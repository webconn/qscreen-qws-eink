#ifndef EINKSCREEN_H
#define EINKSCREEN_H

#include <QScreen>
#include <QPainter>

class QEInkScreen : public QScreen
{
public:
    QEInkScreen(int displayId);
    ~QEInkScreen() {}

    bool connect(const QString &displaySpec);
    void disconnect();

    bool initDevice();
    void shutdownDevice();
    void blit(const QImage &image, const QPoint &topLeft, const QRegion &region);
    void solidFill(const QColor &color, const QRegion &region);
    void exposeRegion(QRegion region, int changing);
    void setMode(int nw, int nh, int nd);

private:
    bool _openFramebuffer(const QString& filename, int *fb, unsigned char **buffer, unsigned char **prebuffer);
    unsigned int _fbShift(int x, int y);
    void _fillPoint(unsigned char colorSpec, int x, int y);

private:
    QString _displaySpec;

protected:
    int d_fd, i_fd;
    unsigned char *d_buffer, *i_buffer, *d_prebuffer, *i_prebuffer;
    size_t buffer_len;
};

#endif // EINKSCREEN_H
