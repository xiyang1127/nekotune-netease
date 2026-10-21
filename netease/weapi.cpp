#include "weapi.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <algorithm>
#include <array>
#include <vector>

namespace netease {
namespace {
const QByteArray nonce = "0CoJUm6Qyw8W8jud";
const QByteArray iv = "0102030405060708";
const char *modulusDecimal =
    "157794750267131502212476817800345498121872783333389747424011531025366277535262539913701806290766479189477533597854989606803194253978660329941980786072432806427833685472618792592200595694346872951301770580765135349259590167490536138082469680638514416594216629258349130257685001248172188325316586707301643237607";

// ── AES-128 ──────────────────────────────────────────────────────────────
constexpr std::array<uint8_t, 256> sbox = [] {
    std::array<uint8_t, 256> table{};
    uint8_t p = 1, q = 1;
    do {
        p = uint8_t(p ^ (p << 1) ^ (p & 0x80 ? 0x1b : 0));
        q ^= uint8_t(q << 1);
        q ^= uint8_t(q << 2);
        q ^= uint8_t(q << 4);
        if (q & 0x80)
            q ^= 0x09;
        const auto rotl = [](uint8_t x, int s) { return uint8_t((x << s) | (x >> (8 - s))); };
        table[p] = uint8_t(q ^ rotl(q, 1) ^ rotl(q, 2) ^ rotl(q, 3) ^ rotl(q, 4) ^ 0x63);
    } while (p != 1);
    table[0] = 0x63;
    return table;
}();
constexpr std::array<uint8_t, 256> inverse = [] {
    std::array<uint8_t, 256> table{};
    for (int i = 0; i < 256; ++i)
        table[sbox[i]] = uint8_t(i);
    return table;
}();
uint8_t xtime(uint8_t x) { return uint8_t((x << 1) ^ (x & 0x80 ? 0x1b : 0)); }
uint8_t multiply(uint8_t a, uint8_t b) {
    uint8_t result = 0;
    while (b) {
        if (b & 1)
            result ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return result;
}
using Block = std::array<uint8_t, 16>;
std::array<uint8_t, 176> expand(const QByteArray &key) {
    std::array<uint8_t, 176> words{};
    for (int i = 0; i < 16; ++i)
        words[i] = uint8_t(key[i]);
    uint8_t rcon = 1;
    for (int i = 16; i < 176; i += 4) {
        uint8_t t[4] = {words[i - 4], words[i - 3], words[i - 2], words[i - 1]};
        if (i % 16 == 0) {
            const uint8_t first = t[0];
            t[0] = uint8_t(sbox[t[1]] ^ rcon);
            t[1] = sbox[t[2]];
            t[2] = sbox[t[3]];
            t[3] = sbox[first];
            rcon = xtime(rcon);
        }
        for (int j = 0; j < 4; ++j)
            words[i + j] = uint8_t(words[i - 16 + j] ^ t[j]);
    }
    return words;
}
void encryptBlock(Block &s, const std::array<uint8_t, 176> &w) {
    for (int i = 0; i < 16; ++i)
        s[i] ^= w[i];
    for (int round = 1; round <= 10; ++round) {
        Block t;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                t[c * 4 + r] = sbox[s[((c + r) % 4) * 4 + r]];
        if (round < 10)
            for (int c = 0; c < 4; ++c) {
                const uint8_t a0 = t[c * 4], a1 = t[c * 4 + 1], a2 = t[c * 4 + 2], a3 = t[c * 4 + 3];
                t[c * 4] = uint8_t(xtime(a0) ^ xtime(a1) ^ a1 ^ a2 ^ a3);
                t[c * 4 + 1] = uint8_t(a0 ^ xtime(a1) ^ xtime(a2) ^ a2 ^ a3);
                t[c * 4 + 2] = uint8_t(a0 ^ a1 ^ xtime(a2) ^ xtime(a3) ^ a3);
                t[c * 4 + 3] = uint8_t(xtime(a0) ^ a0 ^ a1 ^ a2 ^ xtime(a3));
            }
        for (int i = 0; i < 16; ++i)
            s[i] = uint8_t(t[i] ^ w[round * 16 + i]);
    }
}
void decryptBlock(Block &s, const std::array<uint8_t, 176> &w) {
    for (int i = 0; i < 16; ++i)
        s[i] ^= w[160 + i];
    for (int round = 9; round >= 0; --round) {
        Block t;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                t[((c + r) % 4) * 4 + r] = inverse[s[c * 4 + r]];
        for (int i = 0; i < 16; ++i)
            t[i] ^= w[round * 16 + i];
        if (round > 0)
            for (int c = 0; c < 4; ++c) {
                const uint8_t a0 = t[c * 4], a1 = t[c * 4 + 1], a2 = t[c * 4 + 2], a3 = t[c * 4 + 3];
                t[c * 4] = uint8_t(multiply(a0, 14) ^ multiply(a1, 11) ^ multiply(a2, 13) ^ multiply(a3, 9));
                t[c * 4 + 1] = uint8_t(multiply(a0, 9) ^ multiply(a1, 14) ^ multiply(a2, 11) ^ multiply(a3, 13));
                t[c * 4 + 2] = uint8_t(multiply(a0, 13) ^ multiply(a1, 9) ^ multiply(a2, 14) ^ multiply(a3, 11));
                t[c * 4 + 3] = uint8_t(multiply(a0, 11) ^ multiply(a1, 13) ^ multiply(a2, 9) ^ multiply(a3, 14));
            }
        s = t;
    }
}

// ── Minimal unsigned big integer (little-endian 32-bit limbs) ─────────────
using Big = std::vector<uint32_t>;
void trim(Big &a) {
    while (!a.empty() && a.back() == 0)
        a.pop_back();
}
Big fromDecimal(const char *text) {
    Big result;
    for (const char *p = text; *p; ++p) {
        uint64_t carry = uint64_t(*p - '0');
        for (auto &limb : result) {
            const uint64_t value = uint64_t(limb) * 10 + carry;
            limb = uint32_t(value);
            carry = value >> 32;
        }
        if (carry)
            result.push_back(uint32_t(carry));
    }
    return result;
}
Big fromBytes(const QByteArray &bigEndian) {
    Big result((bigEndian.size() + 3) / 4, 0);
    for (qsizetype i = 0; i < bigEndian.size(); ++i) {
        const auto position = bigEndian.size() - 1 - i;
        result[position / 4] |= uint32_t(uint8_t(bigEndian[i])) << (8 * (position % 4));
    }
    trim(result);
    return result;
}
int compare(const Big &a, const Big &b) {
    if (a.size() != b.size())
        return a.size() < b.size() ? -1 : 1;
    for (size_t i = a.size(); i-- > 0;)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}
void subtract(Big &a, const Big &b) {
    int64_t borrow = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        int64_t value = int64_t(a[i]) - borrow - (i < b.size() ? int64_t(b[i]) : 0);
        borrow = value < 0;
        a[i] = uint32_t(value + (borrow ? (int64_t(1) << 32) : 0));
    }
    trim(a);
}
Big multiplyMod(const Big &a, const Big &b, const Big &m) {
    Big product(a.size() + b.size(), 0);
    for (size_t i = 0; i < a.size(); ++i) {
        uint64_t carry = 0;
        for (size_t j = 0; j < b.size(); ++j) {
            const uint64_t value = uint64_t(a[i]) * b[j] + product[i + j] + carry;
            product[i + j] = uint32_t(value);
            carry = value >> 32;
        }
        product[i + b.size()] = uint32_t(carry);
    }
    trim(product);
    // Shift-subtract reduction; the operands are small enough for this to be fast.
    Big remainder;
    for (size_t bit = product.size() * 32; bit-- > 0;) {
        uint32_t carry = (product[bit / 32] >> (bit % 32)) & 1;
        for (auto &limb : remainder) {
            const uint32_t next = limb >> 31;
            limb = (limb << 1) | carry;
            carry = next;
        }
        if (carry)
            remainder.push_back(carry);
        if (compare(remainder, m) >= 0)
            subtract(remainder, m);
    }
    return remainder;
}
QString toHex(const Big &a) {
    QString result;
    for (size_t i = a.size(); i-- > 0;)
        result += QStringLiteral("%1").arg(a[i], 8, 16, QLatin1Char('0'));
    while (result.startsWith('0'))
        result.remove(0, 1);
    return result;
}
} // namespace

QByteArray aesEncrypt(const QByteArray &plain, const QByteArray &key) {
    const auto schedule = expand(key);
    QByteArray padded = plain;
    const int pad = 16 - int(padded.size() % 16);
    padded.append(pad, char(pad));
    Block chain;
    std::copy(iv.begin(), iv.end(), chain.begin());
    QByteArray output;
    for (qsizetype offset = 0; offset < padded.size(); offset += 16) {
        for (int i = 0; i < 16; ++i)
            chain[i] ^= uint8_t(padded[offset + i]);
        encryptBlock(chain, schedule);
        output.append(reinterpret_cast<const char *>(chain.data()), 16);
    }
    return output.toBase64();
}
QByteArray aesDecrypt(const QByteArray &base64, const QByteArray &key) {
    const auto schedule = expand(key);
    const auto data = QByteArray::fromBase64(base64);
    if (data.isEmpty() || data.size() % 16)
        return {};
    Block chain;
    std::copy(iv.begin(), iv.end(), chain.begin());
    QByteArray output;
    for (qsizetype offset = 0; offset < data.size(); offset += 16) {
        Block block, cipher;
        std::copy(data.begin() + offset, data.begin() + offset + 16, block.begin());
        cipher = block;
        decryptBlock(block, schedule);
        for (int i = 0; i < 16; ++i)
            output.append(char(block[i] ^ chain[i]));
        chain = cipher;
    }
    const int pad = uint8_t(output.back());
    if (pad < 1 || pad > 16)
        return {};
    output.chop(pad);
    return output;
}

WeapiBody weapi(const QByteArray &json, QString secret) {
    if (secret.isEmpty()) {
        QByteArray bytes(8, Qt::Uninitialized);
        QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(bytes.data()), 2);
        secret = QString::fromLatin1(bytes.toHex());
    }
    static const QRegularExpression printable("^[\\x20-\\x7e]{16}$");
    if (!printable.match(secret).hasMatch())
        qFatal("Invalid weapi session key");
    const auto key = secret.toLatin1();
    QByteArray reversed = key;
    std::reverse(reversed.begin(), reversed.end());
    static const Big modulus = fromDecimal(modulusDecimal);
    Big base = fromBytes(reversed), result{1};
    for (uint32_t exponent = 65537; exponent; exponent >>= 1) {
        if (exponent & 1)
            result = multiplyMod(result, base, modulus);
        base = multiplyMod(base, base, modulus);
    }
    return {QString::fromLatin1(aesEncrypt(aesEncrypt(json, nonce), key)), toHex(result).rightJustified(256, '0')};
}

QString pictureUrl(const QString &id) {
    static const QRegularExpression digits("^\\d{1,20}$");
    if (!digits.match(id).hasMatch())
        return {};
    const QByteArray magic = "3go8&$8*3*3h0k(2)2";
    QByteArray bytes = id.toLatin1();
    for (qsizetype i = 0; i < bytes.size(); ++i)
        bytes[i] = char(bytes[i] ^ magic[i % magic.size()]);
    auto hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toBase64());
    hash.replace('/', '_').replace('+', '-');
    return QStringLiteral("https://p3.music.126.net/%1/%2.jpg?param=200y200").arg(hash, id);
}
} // namespace netease
