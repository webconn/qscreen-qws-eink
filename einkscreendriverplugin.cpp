#include "einkscreen.h"
#include <QScreenDriverPlugin>

#define KEY "einkscreen"

QT_BEGIN_NAMESPACE

class QEInkScreenDriverPlugin : public QScreenDriverPlugin
{
public:
    QEInkScreenDriverPlugin();
    QScreen* create(const QString& key, int displayId);
    QStringList keys() const;
};

QEInkScreenDriverPlugin::QEInkScreenDriverPlugin() : QScreenDriverPlugin() {}

QScreen* QEInkScreenDriverPlugin::create(const QString &key, int displayId)
{
    if (key.toLower() != KEY) {
        return NULL;
    }
    return new QEInkScreen(displayId);
}

QStringList QEInkScreenDriverPlugin::keys() const
{
    return QStringList() << KEY;
}

Q_EXPORT_PLUGIN2(einkscreen, QEInkScreenDriverPlugin)

QT_END_NAMESPACE
