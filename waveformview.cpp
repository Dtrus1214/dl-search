#include "waveformview.h"
#include "diarizationengine.h"

#include <QtConcurrent>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

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

}

WaveformView::WaveformView(QWidget *parent)
    : QWidget(parent)
    , m_sampleRate(0)
    , m_totalFrames(0)
    , m_zoomFactor(1.0)
    , m_centerNorm(0.5)
    , m_playbackSec(0.0)
    , m_playbackActive(false)
    , m_selectedCursorSec(0.0)
    , m_hasSelection(false)
    , m_selectionAnchorSec(0.0)
    , m_selectionStartSec(0.0)
    , m_selectionEndSec(0.0)
    , m_isSelecting(false)
    , m_isPanning(false)
    , m_panMoved(false)
    , m_panStartX(0)
    , m_panStartCenterNorm(0.5)
    , m_draggingNavBar(false)
    , m_loading(false)
    , m_channels(0)
    , m_bitsPerSample(16)
    , m_dirty(false)
{
    setMinimumHeight(140);
    connect(&m_loadWatcher, &QFutureWatcher<LoadResult>::finished,
            this, &WaveformView::onLoadTaskFinished);
}

WaveformView::~WaveformView()
{
    m_loadWatcher.waitForFinished();
}

bool WaveformView::loadAudio(const QString &audioPath)
{
    if (m_loading) {
        return false;
    }

    clearData();
    m_loading = true;
    update();

    m_loadWatcher.setFuture(QtConcurrent::run([audioPath]() {
        return WaveformView::loadPeaksFromWav(audioPath);
    }));
    return true;
}

void WaveformView::setSegments(const QList<SegmentResult> &segments)
{
    m_segments = segments;
    update();
}

void WaveformView::clearData()
{
    m_peaks.clear();
    m_segments.clear();
    m_sampleRate = 0;
    m_totalFrames = 0;
    m_zoomFactor = 1.0;
    m_centerNorm = 0.5;
    m_selectedCursorSec = 0.0;
    m_hasSelection = false;
    m_selectionAnchorSec = 0.0;
    m_selectionStartSec = 0.0;
    m_selectionEndSec = 0.0;
    m_isSelecting = false;
    m_isPanning = false;
    m_panMoved = false;
    m_panStartX = 0;
    m_panStartCenterNorm = 0.5;
    m_draggingNavBar = false;
    m_loading = false;
    m_channels = 0;
    m_bitsPerSample = 16;
    m_pcmData.clear();
    m_dirty = false;
}

void WaveformView::zoomIn()
{
    zoomByFactor(1.35, width() / 2);
}

void WaveformView::zoomOut()
{
    zoomByFactor(1.0 / 1.35, width() / 2);
}

void WaveformView::resetZoom()
{
    m_zoomFactor = 1.0;
    m_centerNorm = 0.5;
    emit viewWindowChanged(m_centerNorm, viewSpanNorm());
    update();
}

void WaveformView::setViewCenterNorm(double centerNorm)
{
    m_centerNorm = qBound(0.0, centerNorm, 1.0);
    emit viewWindowChanged(m_centerNorm, viewSpanNorm());
    update();
}

double WaveformView::viewCenterNorm() const
{
    return m_centerNorm;
}

bool WaveformView::hasSelection() const
{
    return m_hasSelection && selectionEndSec() > selectionStartSec();
}

bool WaveformView::hasEdits() const
{
    return m_dirty;
}

bool WaveformView::copySelection()
{
    if (!hasSelection() || m_sampleRate <= 0 || m_channels <= 0 || m_bitsPerSample != 16) {
        return false;
    }

    const qint64 bytesPerFrame = static_cast<qint64>(m_channels) * 2;
    const qint64 startFrame = qBound<qint64>(0, static_cast<qint64>(selectionStartSec() * m_sampleRate), m_totalFrames);
    const qint64 endFrame = qBound<qint64>(startFrame, static_cast<qint64>(selectionEndSec() * m_sampleRate), m_totalFrames);
    const qint64 byteStart = startFrame * bytesPerFrame;
    const qint64 byteCount = (endFrame - startFrame) * bytesPerFrame;
    if (byteCount <= 0 || byteStart < 0 || (byteStart + byteCount) > m_pcmData.size()) {
        return false;
    }

    m_clipboardPcm = m_pcmData.mid(static_cast<int>(byteStart), static_cast<int>(byteCount));
    return !m_clipboardPcm.isEmpty();
}

bool WaveformView::cutSelection()
{
    if (!copySelection()) {
        return false;
    }

    const qint64 bytesPerFrame = static_cast<qint64>(m_channels) * 2;
    const qint64 startFrame = qBound<qint64>(0, static_cast<qint64>(selectionStartSec() * m_sampleRate), m_totalFrames);
    const qint64 endFrame = qBound<qint64>(startFrame, static_cast<qint64>(selectionEndSec() * m_sampleRate), m_totalFrames);
    const qint64 byteStart = startFrame * bytesPerFrame;
    const qint64 byteCount = (endFrame - startFrame) * bytesPerFrame;
    if (byteCount <= 0) {
        return false;
    }

    m_pcmData.remove(static_cast<int>(byteStart), static_cast<int>(byteCount));
    m_totalFrames = m_pcmData.size() / bytesPerFrame;
    m_hasSelection = false;
    m_selectedCursorSec = static_cast<double>(startFrame) / m_sampleRate;
    m_dirty = true;
    emit editedChanged(true);
    return rebuildPeaksFromPcm();
}

bool WaveformView::pasteAtCursor()
{
    if (m_clipboardPcm.isEmpty() || m_sampleRate <= 0 || m_channels <= 0 || m_bitsPerSample != 16) {
        return false;
    }

    const qint64 bytesPerFrame = static_cast<qint64>(m_channels) * 2;
    qint64 cursorFrame = qBound<qint64>(0, static_cast<qint64>(m_selectedCursorSec * m_sampleRate), m_totalFrames);
    const qint64 bytePos = cursorFrame * bytesPerFrame;
    if (bytePos < 0 || bytePos > m_pcmData.size()) {
        return false;
    }

    m_pcmData.insert(static_cast<int>(bytePos), m_clipboardPcm);
    m_totalFrames = m_pcmData.size() / bytesPerFrame;
    cursorFrame += (m_clipboardPcm.size() / bytesPerFrame);
    m_selectedCursorSec = static_cast<double>(cursorFrame) / m_sampleRate;
    m_hasSelection = false;
    m_dirty = true;
    emit editedChanged(true);
    return rebuildPeaksFromPcm();
}

bool WaveformView::saveToFile(const QString &path, QString *errorMessage)
{
    if (m_sampleRate <= 0 || m_channels <= 0 || m_bitsPerSample != 16 || m_pcmData.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No editable audio data available.");
        }
        return false;
    }

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to open output file.");
        }
        return false;
    }

    const quint32 dataSize = static_cast<quint32>(m_pcmData.size());
    const quint32 riffSize = 36u + dataSize;
    const quint16 channels = static_cast<quint16>(m_channels);
    const quint32 sampleRate = static_cast<quint32>(m_sampleRate);
    const quint16 bitsPerSample = static_cast<quint16>(m_bitsPerSample);
    const quint16 blockAlign = static_cast<quint16>(channels * (bitsPerSample / 8));
    const quint32 byteRate = sampleRate * blockAlign;

    QByteArray header;
    header.reserve(44);
    header.append("RIFF", 4);
    header.append(static_cast<char>(riffSize & 0xff));
    header.append(static_cast<char>((riffSize >> 8) & 0xff));
    header.append(static_cast<char>((riffSize >> 16) & 0xff));
    header.append(static_cast<char>((riffSize >> 24) & 0xff));
    header.append("WAVE", 4);
    header.append("fmt ", 4);
    header.append(static_cast<char>(16)); header.append(static_cast<char>(0));
    header.append(static_cast<char>(0)); header.append(static_cast<char>(0));
    header.append(static_cast<char>(1)); header.append(static_cast<char>(0));
    header.append(static_cast<char>(channels & 0xff));
    header.append(static_cast<char>((channels >> 8) & 0xff));
    header.append(static_cast<char>(sampleRate & 0xff));
    header.append(static_cast<char>((sampleRate >> 8) & 0xff));
    header.append(static_cast<char>((sampleRate >> 16) & 0xff));
    header.append(static_cast<char>((sampleRate >> 24) & 0xff));
    header.append(static_cast<char>(byteRate & 0xff));
    header.append(static_cast<char>((byteRate >> 8) & 0xff));
    header.append(static_cast<char>((byteRate >> 16) & 0xff));
    header.append(static_cast<char>((byteRate >> 24) & 0xff));
    header.append(static_cast<char>(blockAlign & 0xff));
    header.append(static_cast<char>((blockAlign >> 8) & 0xff));
    header.append(static_cast<char>(bitsPerSample & 0xff));
    header.append(static_cast<char>((bitsPerSample >> 8) & 0xff));
    header.append("data", 4);
    header.append(static_cast<char>(dataSize & 0xff));
    header.append(static_cast<char>((dataSize >> 8) & 0xff));
    header.append(static_cast<char>((dataSize >> 16) & 0xff));
    header.append(static_cast<char>((dataSize >> 24) & 0xff));

    if (out.write(header) != header.size() || out.write(m_pcmData) != m_pcmData.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to write WAV data.");
        }
        return false;
    }

    out.close();
    m_dirty = false;
    emit editedChanged(false);
    return true;
}

void WaveformView::setPlaybackPositionSec(double sec)
{
    m_playbackSec = qMax(0.0, sec);
    m_selectedCursorSec = m_playbackSec;

    // Auto-follow playback when zoomed in so the playhead stays visible.
    if (m_playbackActive && m_zoomFactor > 1.0) {
        const double duration = durationSec();
        if (duration > 0.0) {
            const double playNorm = qBound(0.0, m_playbackSec / duration, 1.0);
            const double span = viewSpanNorm();
            const double start = viewStartNorm();
            const double end = start + span;
            const double leftGuard = start + (span * 0.10);
            const double rightGuard = end - (span * 0.10);

            if (playNorm < leftGuard || playNorm > rightGuard) {
                const double targetCenter = qBound(span / 2.0, playNorm + (span * 0.12), 1.0 - (span / 2.0));
                if (!qFuzzyCompare(1.0 + targetCenter, 1.0 + m_centerNorm)) {
                    m_centerNorm = targetCenter;
                    emit viewWindowChanged(m_centerNorm, viewSpanNorm());
                }
            }
        }
    }
    update();
}

void WaveformView::setPlaybackActive(bool active)
{
    m_playbackActive = active;
    update();
}

void WaveformView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#111723"));

    if (m_peaks.isEmpty()) {
        painter.setPen(QColor("#7f8ca4"));
        const QString text = m_loading
                ? QStringLiteral("Loading waveform...")
                : QStringLiteral("Waveform preview (WAV 16-bit PCM)");
        painter.drawText(rect(), Qt::AlignCenter, text);
        return;
    }

    const QRect bar = navBarRect();
    const QRect area = QRect(8, 8, width() - 16, qMax(20, bar.top() - 10 - 8));
    painter.setPen(Qt::NoPen);

    const double duration = durationSec();
    const double startNorm = viewStartNorm();
    const double spanNorm = viewSpanNorm();
    const double endNorm = startNorm + spanNorm;

    if (duration > 0.0 && !m_segments.isEmpty()) {
        for (int i = 0; i < m_segments.size(); ++i) {
            const SegmentResult &s = m_segments.at(i);
            const double segStartNorm = s.startSec / duration;
            const double segEndNorm = s.endSec / duration;
            if (segEndNorm < startNorm || segStartNorm > endNorm) {
                continue;
            }

            const double clampedStart = qMax(startNorm, segStartNorm);
            const double clampedEnd = qMin(endNorm, segEndNorm);
            const double xStartNorm = (clampedStart - startNorm) / spanNorm;
            const double xEndNorm = (clampedEnd - startNorm) / spanNorm;
            const int x1 = area.left() + static_cast<int>(xStartNorm * area.width());
            const int x2 = area.left() + static_cast<int>(xEndNorm * area.width());
            QColor c = (i % 2 == 0) ? QColor(QStringLiteral("#2d4677")) : QColor(QStringLiteral("#2f6a4f"));
            c.setAlpha(110);
            painter.fillRect(QRect(x1, area.top(), qMax(2, x2 - x1), area.height()), c);
        }
    }

    if (duration > 0.0 && m_hasSelection) {
        const double startSec = selectionStartSec();
        const double endSec = selectionEndSec();
        const double selStartNorm = qBound(0.0, startSec / duration, 1.0);
        const double selEndNorm = qBound(0.0, endSec / duration, 1.0);
        if (selEndNorm >= startNorm && selStartNorm <= endNorm) {
            const double clampedStart = qMax(startNorm, selStartNorm);
            const double clampedEnd = qMin(endNorm, selEndNorm);
            const double xStartNorm = (clampedStart - startNorm) / spanNorm;
            const double xEndNorm = (clampedEnd - startNorm) / spanNorm;
            const int x1 = area.left() + static_cast<int>(xStartNorm * area.width());
            const int x2 = area.left() + static_cast<int>(xEndNorm * area.width());
            painter.fillRect(QRect(x1, area.top(), qMax(1, x2 - x1), area.height()), QColor(102, 217, 239, 55));
        }
    }

    painter.setPen(QColor("#6f94ff"));
    const int midY = area.center().y();
    const int points = m_peaks.size();
    const int startIndex = qBound(0, static_cast<int>(startNorm * points), qMax(0, points - 1));
    const int endIndex = qBound(startIndex + 1, static_cast<int>(endNorm * points), points);
    const int visibleCount = qMax(1, endIndex - startIndex);

    for (int i = 0; i < visibleCount; ++i) {
        const int sampleIndex = startIndex + i;
        const int x = area.left() + (i * area.width()) / qMax(1, visibleCount - 1);
        const int halfHeight = static_cast<int>(m_peaks.at(sampleIndex) * (area.height() / 2.0));
        painter.drawLine(x, midY - halfHeight, x, midY + halfHeight);
    }

    if (duration > 0.0 && m_playbackActive) {
        const double playNorm = qBound(0.0, m_playbackSec / duration, 1.0);
        if (playNorm >= startNorm && playNorm <= endNorm) {
            const double local = (playNorm - startNorm) / spanNorm;
            const int x = area.left() + static_cast<int>(local * area.width());
            painter.setPen(QPen(QColor("#ffd166"), 2));
            painter.drawLine(x, area.top(), x, area.bottom());
        }
    }

    if (duration > 0.0) {
        const double selectedNorm = qBound(0.0, m_selectedCursorSec / duration, 1.0);
        if (selectedNorm >= startNorm && selectedNorm <= endNorm) {
            const double local = (selectedNorm - startNorm) / spanNorm;
            const int x = area.left() + static_cast<int>(local * area.width());
            painter.setPen(QPen(QColor("#66d9ef"), 1, Qt::DashLine));
            painter.drawLine(x, area.top(), x, area.bottom());
        }
    }

    // Embedded zoom/navigation bar.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#1a2230"));
    painter.drawRoundedRect(bar, 4, 4);
    painter.setBrush(QColor("#2b3a57"));
    painter.drawRoundedRect(bar.adjusted(1, 1, -1, -1), 4, 4);

    const double span = viewSpanNorm();
    const double start = viewStartNorm();
    const int vx = bar.left() + static_cast<int>(start * bar.width());
    const int vw = qMax(8, static_cast<int>(span * bar.width()));
    const QRect viewport(vx, bar.top() + 1, qMin(vw, bar.width()), bar.height() - 2);
    painter.setBrush(QColor("#6f94ff"));
    painter.drawRoundedRect(viewport, 3, 3);
}

void WaveformView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        emit contextMenuRequested(mapToGlobal(event->pos()));
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (isInNavBar(event->x(), event->y())) {
            m_draggingNavBar = true;
            const QRect bar = navBarRect();
            const double norm = qBound(0.0,
                                       static_cast<double>(event->x() - bar.left()) / static_cast<double>(qMax(1, bar.width())),
                                       1.0);
            setViewCenterNorm(norm);
            return;
        }

        const double sec = timeAtX(event->x());
        if (sec >= 0.0) {
            m_isSelecting = true;
            m_selectionAnchorSec = sec;
            m_selectionStartSec = sec;
            m_selectionEndSec = sec;
            m_hasSelection = true;
            update();
        }
        m_panMoved = false;
    }
    QWidget::mousePressEvent(event);
}

void WaveformView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_draggingNavBar) {
        const QRect bar = navBarRect();
        const double norm = qBound(0.0,
                                   static_cast<double>(event->x() - bar.left()) / static_cast<double>(qMax(1, bar.width())),
                                   1.0);
        setViewCenterNorm(norm);
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (m_isSelecting) {
        if (qAbs(event->x() - m_panStartX) > 2) {
            m_panMoved = true;
        }
        const double sec = timeAtX(event->x());
        if (sec >= 0.0) {
            m_selectionStartSec = qMin(m_selectionAnchorSec, sec);
            m_selectionEndSec = qMax(m_selectionAnchorSec, sec);
            update();
        }
        QWidget::mouseMoveEvent(event);
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void WaveformView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const bool treatAsClick = !m_panMoved;
    const bool hadSelecting = m_isSelecting;
    const bool hadNavDrag = m_draggingNavBar;
    m_isPanning = false;
    m_isSelecting = false;
    m_draggingNavBar = false;

    if (hadNavDrag) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    if (hadSelecting && m_panMoved) {
        if (m_hasSelection) {
            emit selectionChanged(selectionStartSec(), selectionEndSec());
        }
    } else if (treatAsClick) {
        const double sec = timeAtX(event->x());
        if (sec >= 0.0) {
            m_hasSelection = false;
            m_selectedCursorSec = sec;
            emit cursorSelected(sec);

            const int idx = segmentAtX(event->x());
            if (idx >= 0) {
                emit segmentClicked(idx);
            }
            update();
        }
    }

    QWidget::mouseReleaseEvent(event);
}

void WaveformView::wheelEvent(QWheelEvent *event)
{
    const int deltaY = event->angleDelta().y();
    if (deltaY == 0) {
        QWidget::wheelEvent(event);
        return;
    }

    const double factor = (deltaY > 0) ? 1.20 : (1.0 / 1.20);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const int anchorX = static_cast<int>(event->position().x());
#else
    const int anchorX = event->x();
#endif
    zoomByFactor(factor, anchorX);
    event->accept();
}

int WaveformView::segmentAtX(int x) const
{
    const double duration = durationSec();
    if (duration <= 0.0 || m_segments.isEmpty()) {
        return -1;
    }

    const QRect area = rect().adjusted(8, 8, -8, -8);
    if (!area.contains(x, area.center().y())) {
        return -1;
    }

    const double t = timeAtX(x);
    if (t < 0.0) {
        return -1;
    }
    for (int i = 0; i < m_segments.size(); ++i) {
        const SegmentResult &s = m_segments.at(i);
        if (t >= s.startSec && t <= s.endSec) {
            return i;
        }
    }
    return -1;
}

double WaveformView::timeAtX(int x) const
{
    const double duration = durationSec();
    if (duration <= 0.0) {
        return -1.0;
    }

    const QRect bar = navBarRect();
    const QRect area = QRect(8, 8, width() - 16, qMax(20, bar.top() - 10 - 8));
    if (!area.contains(x, area.center().y())) {
        return -1.0;
    }

    const double startNorm = viewStartNorm();
    const double spanNorm = viewSpanNorm();
    const double localNorm = static_cast<double>(x - area.left()) / static_cast<double>(qMax(1, area.width()));
    const double t = (startNorm + localNorm * spanNorm) * duration;
    return qBound(0.0, t, duration);
}

double WaveformView::durationSec() const
{
    if (m_sampleRate <= 0 || m_totalFrames <= 0) {
        return 0.0;
    }
    return static_cast<double>(m_totalFrames) / static_cast<double>(m_sampleRate);
}

double WaveformView::selectionStartSec() const
{
    return m_hasSelection ? qMin(m_selectionStartSec, m_selectionEndSec) : 0.0;
}

double WaveformView::selectionEndSec() const
{
    return m_hasSelection ? qMax(m_selectionStartSec, m_selectionEndSec) : 0.0;
}

double WaveformView::viewSpanNorm() const
{
    return 1.0 / qMax(1.0, m_zoomFactor);
}

double WaveformView::viewStartNorm() const
{
    const double span = viewSpanNorm();
    const double half = span / 2.0;
    return qBound(0.0, m_centerNorm - half, 1.0 - span);
}

void WaveformView::zoomByFactor(double scale, int anchorX)
{
    const QRect bar = navBarRect();
    const QRect area = QRect(8, 8, width() - 16, qMax(20, bar.top() - 10 - 8));
    if (area.width() <= 0) {
        return;
    }

    const double oldStart = viewStartNorm();
    const double oldSpan = viewSpanNorm();
    const double localNorm = qBound(0.0,
                                    static_cast<double>(anchorX - area.left()) / static_cast<double>(area.width()),
                                    1.0);
    const double anchorAbsoluteNorm = oldStart + localNorm * oldSpan;

    const double newZoom = qBound(1.0, m_zoomFactor * scale, 16.0);
    const double newSpan = 1.0 / newZoom;
    const double newStart = qBound(0.0, anchorAbsoluteNorm - (localNorm * newSpan), 1.0 - newSpan);

    m_zoomFactor = newZoom;
    m_centerNorm = newStart + (newSpan / 2.0);
    emit viewWindowChanged(m_centerNorm, viewSpanNorm());
    update();
}

QRect WaveformView::navBarRect() const
{
    return QRect(8, height() - 18, qMax(20, width() - 16), 10);
}

bool WaveformView::isInNavBar(int x, int y) const
{
    return navBarRect().contains(x, y);
}

bool WaveformView::rebuildPeaksFromPcm()
{
    if (m_sampleRate <= 0 || m_channels <= 0 || m_pcmData.isEmpty()) {
        m_peaks.clear();
        update();
        return false;
    }

    const int peakCount = 1200;
    m_peaks = QVector<float>(peakCount, 0.0f);
    const qint64 bytesPerFrame = static_cast<qint64>(m_channels) * 2;
    const qint64 framesPerBucket = qMax<qint64>(1, m_totalFrames / peakCount);

    for (qint64 frame = 0; frame < m_totalFrames; ++frame) {
        const qint64 sampleOffset = frame * bytesPerFrame;
        const qint16 sample = static_cast<qint16>(
                    static_cast<unsigned char>(m_pcmData[static_cast<int>(sampleOffset)]) |
                    (static_cast<unsigned char>(m_pcmData[static_cast<int>(sampleOffset + 1)]) << 8));
        const float normalized = qAbs(static_cast<float>(sample)) / 32768.0f;
        const int bucket = qMin(peakCount - 1, static_cast<int>(frame / framesPerBucket));
        if (normalized > m_peaks[bucket]) {
            m_peaks[bucket] = normalized;
        }
    }

    update();
    return true;
}

WaveformView::LoadResult WaveformView::loadPeaksFromWav(const QString &audioPath)
{
    LoadResult r{};
    r.ok = false;
    r.sampleRate = 0;
    r.totalFrames = 0;
    r.channels = 0;
    r.bitsPerSample = 16;

    QFile file(audioPath);
    if (!file.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("Unable to open audio file.");
        return r;
    }

    if (file.size() < 44) {
        r.error = QStringLiteral("Invalid WAV file.");
        return r;
    }

    const QByteArray riff = file.read(12);
    if (riff.size() < 12 || riff.mid(0, 4) != "RIFF" || riff.mid(8, 4) != "WAVE") {
        r.error = QStringLiteral("Only WAV files are supported.");
        return r;
    }

    quint16 audioFormat = 0;
    quint16 channels = 0;
    quint16 bitsPerSample = 0;
    qint64 dataStart = -1;
    qint64 dataSize = -1;

    while (!file.atEnd()) {
        const QByteArray chunkHeader = file.read(8);
        if (chunkHeader.size() < 8) {
            break;
        }

        const QByteArray chunkId = chunkHeader.mid(0, 4);
        const quint32 chunkSize = readU32LE(chunkHeader, 4);
        const qint64 chunkDataPos = file.pos();

        if (chunkId == "fmt ") {
            const QByteArray fmt = file.read(chunkSize);
            if (fmt.size() < 16) {
                r.error = QStringLiteral("Corrupt WAV fmt chunk.");
                return r;
            }
            audioFormat = readU16LE(fmt, 0);
            channels = readU16LE(fmt, 2);
            r.sampleRate = static_cast<int>(readU32LE(fmt, 4));
            bitsPerSample = readU16LE(fmt, 14);
            r.channels = channels;
            r.bitsPerSample = bitsPerSample;
        } else if (chunkId == "data") {
            dataStart = chunkDataPos;
            dataSize = static_cast<qint64>(chunkSize);
            break;
        } else {
            file.seek(chunkDataPos + chunkSize);
        }
    }

    if (audioFormat != 1 || bitsPerSample != 16 || channels == 0 || r.sampleRate <= 0) {
        r.error = QStringLiteral("Requires 16-bit PCM WAV.");
        return r;
    }
    if (dataStart < 0 || dataSize <= 0) {
        r.error = QStringLiteral("No WAV data chunk found.");
        return r;
    }

    const int bytesPerFrame = static_cast<int>(channels) * 2;
    r.totalFrames = dataSize / bytesPerFrame;
    if (r.totalFrames <= 0) {
        r.error = QStringLiteral("Audio has no samples.");
        return r;
    }

    const int peakCount = 1200;
    r.peaks = QVector<float>(peakCount, 0.0f);
    const qint64 framesPerBucket = qMax<qint64>(1, r.totalFrames / peakCount);

    if (!file.seek(dataStart)) {
        r.error = QStringLiteral("Failed to seek WAV data.");
        return r;
    }

    const qint64 chunkBytes = 1 * 1024 * 1024;
    qint64 bytesProcessed = 0;
    qint64 frameIndex = 0;
    r.pcmData.reserve(static_cast<int>(qMin<qint64>(dataSize, 256 * 1024 * 1024)));

    while (bytesProcessed < dataSize && !file.atEnd()) {
        const qint64 toRead = qMin(chunkBytes, dataSize - bytesProcessed);
        const QByteArray chunk = file.read(toRead);
        if (chunk.isEmpty()) {
            break;
        }
        r.pcmData.append(chunk);

        const int chunkFrameCount = chunk.size() / bytesPerFrame;
        for (int frame = 0; frame < chunkFrameCount; ++frame, ++frameIndex) {
            const int sampleOffset = frame * bytesPerFrame;
            const qint16 sample = static_cast<qint16>(
                        static_cast<unsigned char>(chunk[sampleOffset]) |
                        (static_cast<unsigned char>(chunk[sampleOffset + 1]) << 8));
            const float normalized = qAbs(static_cast<float>(sample)) / 32768.0f;
            const int bucket = qMin(peakCount - 1, static_cast<int>(frameIndex / framesPerBucket));
            if (normalized > r.peaks[bucket]) {
                r.peaks[bucket] = normalized;
            }
        }

        bytesProcessed += static_cast<qint64>(chunkFrameCount) * bytesPerFrame;
    }

    r.ok = true;
    return r;
}

void WaveformView::onLoadTaskFinished()
{
    const LoadResult r = m_loadWatcher.result();
    m_loading = false;

    if (!r.ok) {
        clearData();
        m_loading = false;
        update();
        emit loadFinished(false, r.error);
        return;
    }

    m_peaks = r.peaks;
    m_sampleRate = r.sampleRate;
    m_totalFrames = r.totalFrames;
    m_channels = r.channels;
    m_bitsPerSample = r.bitsPerSample;
    m_pcmData = r.pcmData;
    m_dirty = false;
    emit viewWindowChanged(m_centerNorm, viewSpanNorm());
    update();
    emit editedChanged(false);
    emit loadFinished(true, QString());
}
