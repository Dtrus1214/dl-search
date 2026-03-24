#ifndef SEARCHENGINE_H
#define SEARCHENGINE_H

#include <QString>
#include <QList>

class SearchEngine
{
public:
    struct TimedWord {
        QString text;
        double startSec = 0.0;
        double endSec = 0.0;
    };

    struct TranscriptData {
        QString text;
        double durationSec = 0.0;
        QList<TimedWord> words;
    };

    explicit SearchEngine();

    bool transcribeToJson(const QString &audioPath, const QString &outputJsonPath, QString *errorMessage = nullptr) const;
    bool loadTranscriptJson(const QString &jsonPath, TranscriptData *data, QString *errorMessage = nullptr) const;
    QString transcriptJsonPathForAudio(const QString &audioPath) const;
    QString modelPath() const;

private:
    QString resolveModelPath() const;
};

#endif // SEARCHENGINE_H
