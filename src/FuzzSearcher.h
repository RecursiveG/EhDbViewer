#ifndef FUZZSEARCHER_H
#define FUZZSEARCHER_H
#include <QChar>
#include <QDebug>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <algorithm>

class FuzzSearcher {
  public:
    // Longest common substring
    static QString Lcs(const QString &a, const QString &b);
    static QString Nfkc(const QString &s);
    static int IndexOfAny(const QString &s, const QSet<QChar> &chars);

    FuzzSearcher();
    bool isBracketBalance(const QString &s);
    // return false if no component can be extracted
    bool pickNextComponent(QString *component_out, bool *in_bracket, QString *s);
    QStringList flatternComponents(const QStringList &slist);
    // return true if success
    bool parsePrefixStem(QString s, QStringList *prefixes, QString *stem);
    template <typename T>
    QList<T> filterMatching(const QList<T> &list, QString base, const std::function<QString(const T &)> &key) {
        QList<T> ret;
        QStringList prefixes;
        QString stem;
        QRegularExpression non_word_char{"\\W", QRegularExpression::UseUnicodePropertiesOption};
        QRegularExpression ignore_prefix{"^[cC][0-9]{2}$"}; // C89 etc.

        base = Nfkc(base);
        if (!parsePrefixStem(base, &prefixes, &stem)) {
            qWarning() << "parsePrefixStem() failed" << base;
            return ret;
        }
        qDebug() << "prefixes:" << prefixes;
        qDebug() << "stem:" << stem;
        for (QString &p : prefixes)
            p.replace(non_word_char, "");
        stem.replace(non_word_char, "");
        qInfo() << prefixes;
        {
            QStringList tmp = prefixes;
            prefixes.clear();
            for (const QString &p : tmp) {
                if (!ignore_prefix.match(p).hasMatch()) {
                    prefixes << p;
                }
            }
        }
        qDebug() << "prefixes after:" << prefixes;
        qDebug() << "stem after:" << stem;

        for (const T &candidate : list) {
            bool matches = false;
            QString s = Nfkc(key(candidate));
            s.replace(non_word_char, "");

            if (include_prefix_matching_) {
                for (const QString &p : qAsConst(prefixes)) {
                    if (s.contains(p)) {
                        matches = true;
                        break;
                    }
                }
            }

            if (!matches) {
                int lcs_len = Lcs(s, stem).size();
                int min_len = std::min(s.size(), stem.size());
                if (s.length() < min_match_threshold_) {
                    matches = !(ignore_too_short_candidates_) && lcs_len >= min_len;
                } else {
                    matches = lcs_len >= min_match_length_ && lcs_len > min_len * min_match_threshold_;
                }
            }

            if (matches)
                ret << candidate;
        }
        return ret;
    }

  private:
    QString parenthesis_ = "()[]{}“”‹›«»（）［］｛｝｟｠「」〈〉《》【】〔〕⦗⦘『』〖〗〘〙｢｣";
    QMap<QChar, QChar> mapping_; // map from open bracket to closing bracket
    QSet<QChar> opennings_;
    QSet<QChar> closings_;

    int min_match_length_ = 4;
    double min_match_threshold_ = 0.4;
    bool ignore_too_short_candidates_ = true;
    bool include_prefix_matching_ = true;
};

#endif // FUZZSEARCHER_H
