#include "searchengine.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QThread>
#include <QVector>
#include <QtGlobal>

#ifdef USE_WHISPER_EMBEDDED
#include "whisper.h"
#endif

namespace {

quint16 readU16LE(const QByteArray &data, int offset)
{
    return static_cast<quint16>(static_cast<unsigned char>(data[offset]) |
                                (static_cast<unsigned char>(data[offset + 1]) << 8));
}

quint32 readU32LE(const QByteArray &data, int offset)
{
    return static_cast<quint32>(static_cast<unsigned char>(data[offset]) |
                                (static_cast<unsigned char>(data[offset + 1]) << 8) |
                                (static_cast<unsigned char>(data[offset + 2]) << 16) |
                                (static_cast<unsigned char>(data[offset + 3]) << 24));
}

bool loadWavMono16k(const QString &path, QVector<float> *outSamples, QString *errorMessage)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to open audio file.");
        }
        return false;
    }
    const QByteArray bytes = f.readAll();
    if (bytes.size() < 44 || bytes.mid(0, 4) != "RIFF" || bytes.mid(8, 4) != "WAVE") {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Only WAV files are supported for embedded engine.");
        }
        return false;
    }

    quint16 audioFormat = 0;
    quint16 channels = 0;
    quint16 bitsPerSample = 0;
    quint32 sampleRate = 0;
    int dataStart = -1;
    int dataSize = -1;
    int pos = 12;
    while (pos + 8 <= bytes.size()) {
        const QByteArray chunkId = bytes.mid(pos, 4);
        const quint32 chunkSize = readU32LE(bytes, pos + 4);
        const int chunkData = pos + 8;
        if (chunkData + static_cast<int>(chunkSize) > bytes.size()) {
            break;
        }
        if (chunkId == "fmt ") {
            if (chunkSize >= 16) {
                const QByteArray fmt = bytes.mid(chunkData, static_cast<int>(chunkSize));
                audioFormat = readU16LE(fmt, 0);
                channels = readU16LE(fmt, 2);
                sampleRate = readU32LE(fmt, 4);
                bitsPerSample = readU16LE(fmt, 14);
            }
        } else if (chunkId == "data") {
            dataStart = chunkData;
            dataSize = static_cast<int>(chunkSize);
            break;
        }
        pos = chunkData + static_cast<int>(chunkSize) + (chunkSize % 2);
    }

    if (audioFormat != 1 || bitsPerSample != 16 || channels < 1 || sampleRate == 0 || dataStart < 0 || dataSize <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Requires 16-bit PCM WAV for embedded engine.");
        }
        return false;
    }

    const int bytesPerFrame = channels * 2;
    const int frameCount = dataSize / bytesPerFrame;
    if (frameCount <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Audio has no sample frames.");
        }
        return false;
    }

    QVector<float> mono;
    mono.resize(frameCount);
    for (int i = 0; i < frameCount; ++i) {
        const int frameOffset = dataStart + (i * bytesPerFrame);
        qint32 sum = 0;
        for (int ch = 0; ch < channels; ++ch) {
            const int sampleOffset = frameOffset + (ch * 2);
            const qint16 sample = static_cast<qint16>(
                static_cast<unsigned char>(bytes[sampleOffset]) |
                (static_cast<unsigned char>(bytes[sampleOffset + 1]) << 8));
            sum += sample;
        }
        const float avg = static_cast<float>(sum) / static_cast<float>(channels);
        mono[i] = avg / 32768.0f;
    }

    if (sampleRate == 16000) {
        *outSamples = mono;
        return true;
    }

    // Linear resample to 16k for whisper.
    const double ratio = 16000.0 / static_cast<double>(sampleRate);
    const int outCount = qMax(1, static_cast<int>(mono.size() * ratio));
    QVector<float> resampled;
    resampled.resize(outCount);
    for (int i = 0; i < outCount; ++i) {
        const double srcPos = static_cast<double>(i) / ratio;
        const int i0 = qBound(0, static_cast<int>(srcPos), mono.size() - 1);
        const int i1 = qMin(i0 + 1, mono.size() - 1);
        const float t = static_cast<float>(srcPos - i0);
        resampled[i] = mono[i0] * (1.0f - t) + mono[i1] * t;
    }
    *outSamples = resampled;
    return true;
}

}

SearchEngine::SearchEngine() = default;

bool SearchEngine::transcribeToJson(const QString &audioPath, const QString &outputJsonPath, QString *errorMessage) const
{
    const QString model = resolveModelPath();
    if (!QFileInfo::exists(model)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Missing Whisper model. Set WHISPER_MODEL_PATH or put model at: %1").arg(model);
        }
        return false;
    }

#ifndef USE_WHISPER_EMBEDDED
    Q_UNUSED(audioPath)
    Q_UNUSED(outputJsonPath)
    if (errorMessage) {
        *errorMessage = QStringLiteral("Embedded whisper engine not linked. Set WHISPER_CPP_DIR and rebuild.");
    }
    return false;
#else
    QVector<float> samples;
    QString wavError;
    if (!loadWavMono16k(audioPath, &samples, &wavError)) {
        if (errorMessage) {
            *errorMessage = wavError;
        }
        return false;
    }

    whisper_context_params cparams = whisper_context_default_params();
    struct whisper_context *ctx = whisper_init_from_file_with_params(model.toUtf8().constData(), cparams);
    if (!ctx) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to initialize whisper context.");
        }
        return false;
    }

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.translate = false;
    params.no_context = true;
    params.single_segment = false;
    params.max_len = 0;
    params.token_timestamps = true;
    params.thold_pt = 0.01f;
    params.thold_ptsum = 0.01f;
    params.n_threads = qMax(1, QThread::idealThreadCount());

    const QByteArray envLang = qEnvironmentVariable("WHISPER_LANG").toUtf8();
    if (!envLang.isEmpty()) {
        params.language = envLang.constData();
    }

    const int rc = whisper_full(ctx, params, samples.constData(), samples.size());
    if (rc != 0) {
        whisper_free(ctx);
        if (errorMessage) {
            *errorMessage = QStringLiteral("whisper_full failed.");
        }
        return false;
    }

    QJsonArray segments;
    QJsonArray words;
    QStringList textParts;
    const int nSegments = whisper_full_n_segments(ctx);
    for (int i = 0; i < nSegments; ++i) {
        const char *segTextC = whisper_full_get_segment_text(ctx, i);
        const QString segText = QString::fromUtf8(segTextC ? segTextC : "").trimmed();
        const int t0 = whisper_full_get_segment_t0(ctx, i);
        const int t1 = whisper_full_get_segment_t1(ctx, i);
        const double startSec = t0 * 0.01;
        const double endSec = t1 * 0.01;

        if (!segText.isEmpty()) {
            textParts.append(segText);
        }

        QJsonObject segObj;
        segObj.insert("start", startSec);
        segObj.insert("end", endSec);
        segObj.insert("text", segText);
        segments.append(segObj);

        QList<QPair<QString, QPair<double, double>>> segmentWords;
        const int nTokens = whisper_full_n_tokens(ctx, i);
        for (int j = 0; j < nTokens; ++j) {
            const auto td = whisper_full_get_token_data(ctx, i, j);
            const char *tokenTextC = whisper_token_to_str(ctx, td.id);
            const QString tokenText = QString::fromUtf8(tokenTextC ? tokenTextC : "").trimmed();
            if (tokenText.isEmpty()) {
                continue;
            }
            // Skip control markers such as [_BEG_] and [_TT_xxx].
            if (tokenText.startsWith("[_") && tokenText.endsWith("]")) {
                continue;
            }
            if (td.t0 >= 0 && td.t1 >= td.t0) {
                segmentWords.append({tokenText, {td.t0 * 0.01, td.t1 * 0.01}});
            }
        }

        if (!segmentWords.isEmpty()) {
            for (const auto &entry : segmentWords) {
                QJsonObject wordObj;
                wordObj.insert("word", entry.first);
                wordObj.insert("start", entry.second.first);
                wordObj.insert("end", entry.second.second);
                words.append(wordObj);
            }
        } else if (!segText.isEmpty() && endSec > startSec) {
            // Fallback: distribute word timestamps across segment duration.
            const QStringList roughWords = segText.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (!roughWords.isEmpty()) {
                const double span = qMax(0.01, endSec - startSec);
                const double step = span / roughWords.size();
                for (int w = 0; w < roughWords.size(); ++w) {
                    const QString word = roughWords.at(w).trimmed();
                    if (word.isEmpty()) {
                        continue;
                    }
                    QJsonObject wordObj;
                    wordObj.insert("word", word);
                    wordObj.insert("start", startSec + (w * step));
                    wordObj.insert("end", qMin(endSec, startSec + ((w + 1) * step)));
                    words.append(wordObj);
                }
            }
        }
    }

    QJsonObject root;
    root.insert("audio_path", QFileInfo(audioPath).absoluteFilePath());
    root.insert("duration_sec", static_cast<double>(samples.size()) / 16000.0);
    root.insert("text", textParts.join(' ').trimmed());
    root.insert("segments", segments);
    root.insert("words", words);
    root.insert("engine", QStringLiteral("whisper.cpp-embedded"));

    whisper_free(ctx);

    QFile outFile(outputJsonPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot write transcript JSON.");
        }
        return false;
    }
    outFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
#endif
}

bool SearchEngine::loadTranscriptJson(const QString &jsonPath, TranscriptData *data, QString *errorMessage) const
{
    if (!data) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No destination for transcript data.");
        }
        return false;
    }

    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to open transcript JSON.");
        }
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Transcript JSON is invalid.");
        }
        return false;
    }

    const QJsonObject root = doc.object();
    data->text = root.value("text").toString();
    data->durationSec = 0.0;
    data->words.clear();

    QJsonArray segments = root.value("segments").toArray();
    if (segments.isEmpty()) {
        segments = root.value("transcription").toArray();
    }
    QStringList textParts;
    for (const QJsonValue &value : segments) {
        const QJsonObject seg = value.toObject();
        const QString segText = seg.value("text").toString().trimmed();
        if (!segText.isEmpty()) {
            textParts.append(segText);
        }
        double t0 = seg.value("start").toDouble();
        double t1 = seg.value("end").toDouble();
        if (qFuzzyIsNull(t0) && qFuzzyIsNull(t1)) {
            t0 = seg.value("offsets").toObject().value("from").toDouble() / 1000.0;
            t1 = seg.value("offsets").toObject().value("to").toDouble() / 1000.0;
        }
        data->durationSec = qMax(data->durationSec, t1);
    }

    const QJsonArray words = root.value("words").toArray();
    for (const QJsonValue &value : words) {
        const QJsonObject obj = value.toObject();
        TimedWord w;
        w.text = obj.value("word").toString();
        w.startSec = obj.value("start").toDouble();
        w.endSec = obj.value("end").toDouble(w.startSec);
        if (!w.text.isEmpty()) {
            data->words.append(w);
        }
    }

    if (data->text.isEmpty()) {
        data->text = textParts.join(' ').trimmed();
    }
    return !data->text.isEmpty();
}

QString SearchEngine::transcriptJsonPathForAudio(const QString &audioPath) const
{
    const QFileInfo info(audioPath);
    return info.absolutePath() + "/" + info.completeBaseName() + ".transcript.json";
}

QString SearchEngine::modelPath() const
{
    return resolveModelPath();
}

QString SearchEngine::resolveModelPath() const
{
    const QString env = qEnvironmentVariable("WHISPER_MODEL_PATH");
    if (!env.isEmpty()) {
        return env;
    }
    QString local = QCoreApplication::applicationDirPath() + "/models/ggml-small.en.bin";
    if (QFileInfo::exists(local)) {
        return local;
    }
    local = "models/ggml-small.en.bin";
    return local;
}
