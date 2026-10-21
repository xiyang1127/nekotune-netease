#pragma once
#include <QByteArray>
#include <QString>

namespace netease {
struct WeapiBody {
    QString params;
    QString encSecKey;
};
/// NetEase weapi envelope: AES-128-CBC twice (nonce, then the session secret) and the reversed
/// secret raised to 65537 modulo NetEase's public modulus (textbook RSA, no padding).
/// secret must be 16 printable ASCII characters; empty picks a random one.
WeapiBody weapi(const QByteArray &json, QString secret = {});
/// AES-128-CBC with PKCS#7 padding and NetEase's fixed IV, base64 encoded.
QByteArray aesEncrypt(const QByteArray &plain, const QByteArray &key);
QByteArray aesDecrypt(const QByteArray &base64, const QByteArray &key);
/// Cover URL derived from a picture id; empty for non-numeric ids.
QString pictureUrl(const QString &id);
} // namespace netease
