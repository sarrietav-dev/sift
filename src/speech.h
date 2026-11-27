#pragma once

#include <QString>

// Text as Kokoro should hear it.
//
// Kokoro reads a bare Roman numeral as letters, so "World War II" comes out as
// "world war eye eye". A numeral that follows a label such as "Part", "War", or
// "Chapter" is a number, and is replaced with its word. Same rules as
// web/src/speech.js; change both together.
namespace speech {

QString spokenNumerals(const QString &text);

}
