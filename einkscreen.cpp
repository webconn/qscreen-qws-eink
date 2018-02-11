#include "einkscreen.h"

#include <QDebug>

#ifndef QT_NO_QWS_LINUXFB
//#include "qmemorymanager_qws.h"
#include "qwsdisplay_qws.h"
#include "qpixmap.h"

#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/kd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <signal.h>

#if !defined(Q_OS_DARWIN) && !defined(Q_OS_FREEBSD)
#include <linux/fb.h>
#endif

#define FB_SCREEN_REDRAW 0x46d1

QT_BEGIN_NAMESPACE


QEInkScreen::QEInkScreen(int displayId)
    : QScreen(displayId)
    , d_fd(-1), i_fd(-1)
    , d_buffer((unsigned char *) MAP_FAILED)
    , i_buffer((unsigned char *) MAP_FAILED)
{

}

bool QEInkScreen::connect(const QString &displaySpec)
{
    _displaySpec = displaySpec;

    const QStringList args = displaySpec.split(QLatin1Char(':'));

    QString ddev = QLatin1String("/dev/fb0"), idev = QLatin1String("/dev/fb1");
    int count = 0;
    foreach(QString d, args) {
        if (d.startsWith(QLatin1Char('/'))) {
            switch (count) {
            case 0:
                ddev = d;
                break;
            case 1:
                idev = d;
                break;
            default:
                qCritical("Unknown third file argument");
                return false;
            }
            count++;
        }
    }

    w = h = 0;
    if (!_openFramebuffer(ddev, &d_fd, &d_buffer, &d_prebuffer)) {
        return false;
    }

    if (!_openFramebuffer(idev, &i_fd, &i_buffer, &i_prebuffer)) {
        return false;
    }

    QRegExp mmWidthRx(QLatin1String("mmWidth=?(\\d+)"));
        int dimIdxW = args.indexOf(mmWidthRx);
        QRegExp mmHeightRx(QLatin1String("mmHeight=?(\\d+)"));
        int dimIdxH = args.indexOf(mmHeightRx);
        if (dimIdxW >= 0) {
            mmWidthRx.exactMatch(args.at(dimIdxW));
            physWidth = mmWidthRx.cap(1).toInt();
            if (dimIdxH < 0)
                physHeight = dh*physWidth/dw;
        }
        if (dimIdxH >= 0) {
            mmHeightRx.exactMatch(args.at(dimIdxH));
            physHeight = mmHeightRx.cap(1).toInt();
            if (dimIdxW < 0)
                physWidth = dw*physHeight/dh;
        }
        if (dimIdxW < 0 && dimIdxH < 0) {
            if (w != 0 && h != 0) {
                physWidth = w;
                physHeight = h;
            } else {
                const int dpi = 72;
                physWidth = qRound(dw * 25.4 / dpi);
                physHeight = qRound(dh * 25.4 / dpi);
            }
        }

    qDebug() << "QEInkScreen::connect - success";

    return true;
}

bool QEInkScreen::initDevice()
{
#ifndef QT_NO_QWS_CURSOR
    QScreenCursor::initSoftwareCursor();
#endif
    blank(false);

    return true;
}

bool QEInkScreen::_openFramebuffer(const QString &filename, int *fd, unsigned char **buffer)
{
    /* open framebuffer file */
    *fd = open(filename.toLatin1().constData(), O_RDWR);
    if (*fd == -1) {
        perror("QEInkScreen::connect");
        qCritical("Error opening framebuffer device %s", qPrintable(filename));
        return false;
    }

    /* check if screen geometry is already loaded */
    if (w == 0 || h == 0) {
        ::fb_fix_screeninfo finfo;
        ::fb_var_screeninfo vinfo;

        memset(&vinfo, 0, sizeof(vinfo));
        memset(&finfo, 0, sizeof(finfo));

        /* get fb fixed info */
        if (ioctl(*fd, FBIOGET_FSCREENINFO, &finfo)) {
            perror("QEinkScreen::connect");
            qCritical("Error reading fixed information from device");
            return false;
        }

        if (ioctl(*fd, FBIOGET_VSCREENINFO, &vinfo)) {
            perror("QEInkScreen::connect");
            qCritical("Error reading variable information from device");
            return false;
        }

        w = dw = vinfo.xres;
        h = dh = vinfo.yres;
        d = vinfo.bits_per_pixel;

        lstep = finfo.line_length;
        if (!lstep) {
            lstep = w * 8 / d; // 8 bits in byte, sometimes pixels are smaller than 1 byte
        }

        physWidth = vinfo.width;
        physHeight = vinfo.height;

        data = (unsigned char *) -1;

        buffer_len = finfo.smem_len;
    }

    /* map it to memory */
    *buffer = (unsigned char *) mmap(0, buffer_len, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);
    if (*buffer == MAP_FAILED) {
        perror("QEInkScreen::connect");
        qCritical("Error mapping framebuffer to memory");
        return false;
    }

    return true;
}

void QEInkScreen::disconnect()
{
    if (d_buffer != MAP_FAILED) {
        munmap((char *) d_buffer, buffer_len);
    }

    if (i_buffer != MAP_FAILED) {
        munmap((char *) i_buffer, buffer_len);
    }

    close(d_fd);
    close(i_fd);
}

unsigned int QEInkScreen::_fbShift(int x, int y)
{
    /* this fixes strange bug with Inch S5i framebuffer where first row is addressed as last */
    y += 1;
    if (y == h) {
        y = 0;
    }

    return y * lstep + x / 2;
}

void QEInkScreen::_fillPoint(unsigned char colorSpec, int x, int y)
{
    unsigned int ad = _fbShift(x, y);
    unsigned char c = d_buffer[ad];

    if (x & 1) {
        c &= 0xf0;
        c |= colorSpec & 0xf;
    } else {
        c &= 0x0f;
        c |= (colorSpec << 4) & 0xf;
    }

    d_buffer[ad] = c;
    i_buffer[ad] = (~c) & 0xff;
}

void QEInkScreen::solidFill(const QColor &color, const QRegion &region)
{
    unsigned char colorSpec = qGray(color.rgb()) / 16;

    /* let's just scan region and print specific pixels */
    int x, y;
    int x1, y1, x2, y2;
    region.boundingRect().getCoords(&x1, &y1, &x2, &y2);

    for (y = y1; y < y2; y++) {
        for (x = x1; x < x2; x++) {
            if (region.contains(QPoint(x, y))) {
                _fillPoint(colorSpec, x, y);
            }
        }
    }
}

void QEInkScreen::blit(const QImage &image, const QPoint &topLeft, const QRegion &region)
{
    /* let's just scan region and print specific pixels */
    int x, y;
    int x1, y1, x2, y2;
    region.boundingRect().getCoords(&x1, &y1, &x2, &y2);

    for (y = y1; y < y2; y++) {
        for (x = x1; x < x2; x++) {
            if (region.contains(QPoint(x, y))) {
                unsigned char colorSpec = qGray(image.pixel(x, y)) / 16;
                _fillPoint(colorSpec, x - x1 + topLeft.x(), y - y1 + topLeft.y());
            }
        }
    }
}

void QEInkScreen::exposeRegion(QRegion region, int changing)
{
    /* expose it as is and push update event to framebuffer */
    QScreen::exposeRegion(region, changing);

    ioctl(d_fd, FB_SCREEN_REDRAW, 0);
}

/* this is stolen from QLinuxFbScreen with hope that this will not break everything */
void QEInkScreen::setMode(int nw, int nh, int nd)
{
    qDebug("QEInkScreen::setMode(%d, %d, %d) - naive guys...", nw, nh, nd);

    disconnect();
    connect(_displaySpec);
    exposeRegion(region(), 0);

    return; // and let die
#if 0
        if (d_fd == -1)
            return;

        ::fb_fix_screeninfo finfo;
        ::fb_var_screeninfo vinfo;
        //#######################
        // Shut up Valgrind
        memset(&vinfo, 0, sizeof(vinfo));
        memset(&finfo, 0, sizeof(finfo));
        //#######################

        if (ioctl(d_fd, FBIOGET_VSCREENINFO, &vinfo)) {
            perror("QLinuxFbScreen::setMode");
            qFatal("Error reading variable information in mode change");
        }

        vinfo.xres=nw;
        vinfo.yres=nh;
        vinfo.bits_per_pixel=nd;

        if (ioctl(d_fd, FBIOPUT_VSCREENINFO, &vinfo)) {
            perror("QLinuxFbScreen::setMode");
            qCritical("Error writing variable information in mode change");
        }

        if (ioctl(d_fd, FBIOGET_VSCREENINFO, &vinfo)) {
            perror("QLinuxFbScreen::setMode");
            qFatal("Error reading changed variable information in mode change");
        }

        if (ioctl(d_fd, FBIOGET_FSCREENINFO, &finfo)) {
            perror("QLinuxFbScreen::setMode");
            qFatal("Error reading fixed information");
        }

        disconnect();
        connect(d_ptr->displaySpec);
        exposeRegion(region(), 0);
#endif
}

#endif // QT_NO_QWS_LINUXFB
