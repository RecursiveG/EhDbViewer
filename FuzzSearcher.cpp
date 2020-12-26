#include "FuzzSearcher.h"
#include <cinttypes>
#include <memory>

QString FuzzSearcher::Lcs(const QString &a, const QString &b) {
    if (a.isEmpty() || b.isEmpty())
        return "";
    int end_loc = 0;
    int len = 0;
    auto dp = std::make_unique<int[]>(a.size());
    for (int i = 0; i < a.size(); i++) {
        if (a[i] == b[0]) {
            dp[i] = 1;
            end_loc = i;
            len = 1;
        } else {
            dp[i] = 0;
        }
    }
    for (int j = 1; j < b.size(); j++) {
        for (int i = a.size() - 1; i > 0; i--) {
            if (a[i] == b[j]) {
                dp[i] = dp[i - 1] + 1;
                if (dp[i] > len) {
                    len = dp[i];
                    end_loc = i;
                }
            } else {
                dp[i] = 0;
            }
        }
        if (a[0] == b[j]) {
            dp[0] = 1;
            if (len < 1) {
                len = 1;
                end_loc = 0;
            }
        }
    }
    return QStringRef{&a, end_loc - len + 1, len}.toString();
}

QString FuzzSearcher::Nfkc(const QString &s) { return s.normalized(QString::NormalizationForm_KC); }

int FuzzSearcher::IndexOfAny(const QString &s, const QSet<QChar> &chars) {
    if (s.isEmpty())
        return -1;
    for (int i = 0; i < s.size(); i++) {
        if (chars.contains(s.at(i)))
            return i;
    }
    return -1;
}

FuzzSearcher::FuzzSearcher() {
    for (int i = 0; i < parenthesis_.size(); i = i + 2) {
        opennings_.insert(parenthesis_.at(i));
        closings_.insert(parenthesis_.at(i + 1));
        mapping_.insert(parenthesis_.at(i), parenthesis_.at(i + 1));
    }
}

bool FuzzSearcher::isBracketBalance(const QString &s) {
    QList<QChar> expected_endings;
    for (QChar ch : s) {
        if (mapping_.contains(ch)) {
            expected_endings.append(mapping_[ch]);
        } else if (closings_.contains(ch)) {
            if (expected_endings.isEmpty())
                return false;
            if (expected_endings[expected_endings.size() - 1] != ch)
                return false;
            expected_endings.pop_back();
        }
    }
    return expected_endings.isEmpty();
}

bool FuzzSearcher::pickNextComponent(QString *component_out, bool *in_bracket, QString *s) {
    if (s->isEmpty())
        return false;
    if (!mapping_.contains(s->at(0))) {
        int len = IndexOfAny(*s, opennings_);
        if (len < 0)
            len = s->size();
        *component_out = s->left(len);
        *in_bracket = false;
        *s = s->right(s->size() - len);
        return true;
    } else {
        QList<QChar> expected_endings;
        expected_endings.append(mapping_[s->at(0)]);
        for (int i = 1; i < s->size(); i++) {
            QChar ch = s->at(i);
            if (mapping_.contains(ch)) {
                expected_endings.append(mapping_[ch]);
            } else if (closings_.contains(ch)) {
                if (expected_endings.isEmpty())
                    throw std::runtime_error("missing opening bracket");
                if (expected_endings[expected_endings.size() - 1] != ch)
                    throw std::runtime_error("closing bracket not match");
                expected_endings.pop_back();
                if (expected_endings.isEmpty()) {
                    *component_out = s->mid(1, i - 1);
                    *in_bracket = true;
                    *s = s->mid(i + 1);
                    return true;
                }
            }
        }
        throw std::runtime_error("missing closing bracket");
    }
}

QStringList FuzzSearcher::flatternComponents(const QStringList &slist) {
    QStringList ret;
    for (QString s : slist) {
        QString comp;
        bool b;
        while (pickNextComponent(&comp, &b, &s)) {
            comp = comp.trimmed();
            if (!comp.isEmpty())
                ret << comp;
        }
    }
    return ret;
}

bool FuzzSearcher::parsePrefixStem(QString s, QStringList *prefixes, QString *stem) {
    if (!isBracketBalance(s))
        return false;
    prefixes->clear();
    *stem = "";
    s = s.trimmed();

    QString component;
    bool inb;
    while (pickNextComponent(&component, &inb, &s)) {
        if (inb) {
            prefixes->append(component.trimmed());
        } else {
            *stem = component.trimmed();
            break;
        }
        s = s.trimmed();
    }
    *prefixes = flatternComponents(*prefixes);
    return true;
}
