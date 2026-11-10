#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <memory>

struct SharedSender {
    QString id;
    QString label;
};

class SharedInput {
public:
    virtual ~SharedInput() = default;
    virtual QImage frame() = 0;
};

QList<SharedSender> sharedSenders();
std::unique_ptr<SharedInput> openSharedSender(const QString &id);
