#ifndef WAVEFORMVIEW_H
#define WAVEFORMVIEW_H

#include "diarizationengine.h"

#include <QList>
#include <QFutureWatcher>
#include <QPoint>
#include <QVector>
#include <QWidget>

class WaveformView : public QWidget
{
    Q_OBJECT
public:
    explicit WaveformView(QWidget *parent = nullptr);
    ~WaveformView();

    bool loadAudio(const QString &audioPath);
    void setSegments(const QList<SegmentResult> &segments);
    void clearData();
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void setViewCenterNorm(double centerNorm);
    double viewCenterNorm() const;
    bool hasSelection() const;
    bool hasEdits() const;
    bool cutSelection();
    bool copySelection();
    bool pasteAtCursor();
    bool saveToFile(const QString &path, QString *errorMessage = nullptr);

signals:
    void segmentClicked(int index);
    void cursorSelected(double sec);
    void selectionChanged(double startSec, double endSec);
    void loadFinished(bool ok, const QString &message);
    void viewWindowChanged(double centerNorm, double spanNorm);
    void editedChanged(bool dirty);
    void contextMenuRequested(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    struct LoadResult {
        bool ok;
        QString error;
        QVector<float> peaks;
        int sampleRate;
        qint64 totalFrames;
        int channels;
        int bitsPerSample;
        QByteArray pcmData;
    };

    int segmentAtX(int x) const;
    double timeAtX(int x) const;
    double durationSec() const;
    double viewStartNorm() const;
    double viewSpanNorm() const;
    void zoomByFactor(double scale, int anchorX);
    double selectionStartSec() const;
    double selectionEndSec() const;
    bool rebuildPeaksFromPcm();
    QRect navBarRect() const;
    bool isInNavBar(int x, int y) const;
    static LoadResult loadPeaksFromWav(const QString &audioPath);
    void onLoadTaskFinished();

    QVector<float> m_peaks;
    QList<SegmentResult> m_segments;
    int m_sampleRate;
    qint64 m_totalFrames;
    double m_zoomFactor;
    double m_centerNorm;
    double m_playbackSec;
    bool m_playbackActive;
    double m_selectedCursorSec;
    bool m_hasSelection;
    double m_selectionAnchorSec;
    double m_selectionStartSec;
    double m_selectionEndSec;
    bool m_isSelecting;
    bool m_isPanning;
    bool m_panMoved;
    int m_panStartX;
    double m_panStartCenterNorm;
    bool m_draggingNavBar;
    bool m_loading;
    QFutureWatcher<LoadResult> m_loadWatcher;
    int m_channels;
    int m_bitsPerSample;
    QByteArray m_pcmData;
    QByteArray m_clipboardPcm;
    bool m_dirty;

public:
    void setPlaybackPositionSec(double sec);
    void setPlaybackActive(bool active);
};

#endif // WAVEFORMVIEW_H
