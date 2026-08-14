#pragma once
#include <windows.h>

// Oriel's translations.
//
// Japanese is the source language and doubles as the key: T(L"設定") returns
// "Settings" in English and the key itself in Japanese. That keeps the call
// sites readable, needs no key catalogue to cross-reference, and makes an
// untranslated string degrade into the original rather than into a key name.
//
// T returns a pointer with static storage duration, so it is safe to hold.
namespace oriel {

enum class Lang { Auto = 0, Ja = 1, En = 2 };

// Resolves Auto from the OS UI language. Call once, before anything draws.
void i18nInit(Lang preference);
Lang i18nLang();                     // resolved: never Auto

const wchar_t* T(const wchar_t* ja);

} // namespace oriel
