#include "speech.h"

#include <QSet>
#include <QStringList>

namespace {

// Words after which a Roman numeral is a number, not letters.
const QSet<QString> &labels() {
    static const QSet<QString> words{
        "part", "parts", "chapter", "chapters", "section", "sections", "book", "books",
        "volume", "volumes", "vol", "title", "titles", "appendix", "appendices", "annex", "annexes",
        "act", "acts", "scene", "scenes", "article", "articles", "clause", "clauses",
        "exhibit", "exhibits", "figure", "figures", "table", "tables", "step", "steps",
        "phase", "phases", "level", "levels", "tier", "tiers", "stage", "stages",
        "unit", "units", "module", "modules", "rule", "rules", "division", "divisions",
        "grade", "grades", "class", "classes", "item", "items", "question", "questions",
        "example", "examples", "exercise", "exercises", "war", "wars", "episode", "episodes",
        "season", "seasons", "round", "rounds", "type", "types", "category", "categories",
    };
    return words;
}

// Words that keep a list of numerals going: "Parts I and II".
const QSet<QString> &joiners() {
    static const QSet<QString> words{"and", "or", "through", "to", "versus", "vs"};
    return words;
}

const QStringList &numberWords() {
    static const QStringList words{
        "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
        "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen",
        "eighteen", "nineteen", "twenty", "twenty-one", "twenty-two", "twenty-three",
        "twenty-four", "twenty-five", "twenty-six", "twenty-seven", "twenty-eight",
        "twenty-nine", "thirty", "thirty-one", "thirty-two", "thirty-three", "thirty-four",
        "thirty-five", "thirty-six", "thirty-seven", "thirty-eight", "thirty-nine",
    };
    return words;
}

QString toRoman(int value) {
    static const struct { int step; const char *digits; } steps[] = {
        {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"},
        {50, "L"}, {40, "XL"}, {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"},
    };
    QString roman;
    for (const auto &step : steps) {
        while (value >= step.step) {
            roman += QLatin1String(step.digits);
            value -= step.step;
        }
    }
    return roman;
}

// "III" → "three". Only canonical numerals we have a word for.
QString romanWord(const QString &token) {
    int total = 0;
    int previous = 0;
    for (int i = token.size() - 1; i >= 0; --i) {
        int value = 0;
        switch (token.at(i).toUpper().unicode()) {
        case 'I': value = 1; break;
        case 'V': value = 5; break;
        case 'X': value = 10; break;
        case 'L': value = 50; break;
        case 'C': value = 100; break;
        case 'D': value = 500; break;
        case 'M': value = 1000; break;
        default: return {};
        }
        if (value < previous) {
            total -= value;
        } else {
            total += value;
            previous = value;
        }
    }
    if (total <= 0 || total > numberWords().size() || toRoman(total).compare(token, Qt::CaseInsensitive) != 0)
        return {};
    return numberWords().at(total - 1);
}

} // namespace

namespace speech {

QString spokenNumerals(const QString &text) {
    QString out;
    out.reserve(text.size() + 8);
    // After a label, a numeral is a number; a joiner keeps a list going.
    bool afterLabel = false;
    bool inList = false;
    // "I" is also the pronoun: "the article I wrote". Read it as one only
    // after a capitalised label, as in "World War I" or "Part I".
    bool capitalLabel = false;
    int at = 0;
    while (at < text.size()) {
        int end = at;
        while (end < text.size() && !text.at(end).isSpace())
            ++end;
        int spaceEnd = end;
        while (spaceEnd < text.size() && text.at(spaceEnd).isSpace())
            ++spaceEnd;
        const QString body = text.mid(at, end - at);
        const QString space = text.mid(end, spaceEnd - end);
        at = spaceEnd;

        int start = 0;
        while (start < body.size() && !body.at(start).isLetterOrNumber())
            ++start;
        int stop = body.size();
        while (stop > start && !body.at(stop - 1).isLetterOrNumber())
            --stop;
        const QString core = body.mid(start, stop - start);
        if (core.isEmpty()) {
            out += body + space;
            continue;
        }
        const QString word = core.toLower();
        if (labels().contains(word)) {
            afterLabel = true;
            inList = false;
            capitalLabel = core.at(0).isUpper();
        } else if ((afterLabel || inList) && joiners().contains(word)) {
            afterLabel = false;
            inList = true;
        } else if (afterLabel || inList) {
            const QString said = core == "I" && !capitalLabel ? QString() : romanWord(core);
            afterLabel = false;
            inList = !said.isEmpty();
            if (!said.isEmpty()) {
                // A comma or an "and" may come before the next numeral.
                out += body.left(start) + said + body.mid(stop) + space;
                continue;
            }
        }
        out += body + space;
    }
    return out;
}

} // namespace speech
