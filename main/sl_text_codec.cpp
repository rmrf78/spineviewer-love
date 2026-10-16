#include "sl_text_codec.h"

#include <limits>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace
{
	int ClampLength(size_t length)
	{
		constexpr size_t kMaxInt = static_cast<size_t>((std::numeric_limits<int>::max)());
		return length > kMaxInt ? 0 : static_cast<int>(length);
	}

#if defined(_WIN32)

	std::wstring Utf8BytesToWide(const char* text, size_t textLength)
	{
		if (text == nullptr || textLength == 0)
			return {};

		const int byteCount = ClampLength(textLength);
		if (byteCount <= 0)
			return {};

		const int charCount = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, byteCount, nullptr, 0);
		if (charCount <= 0)
			return {};

		std::wstring wideText(static_cast<size_t>(charCount), L'\0');
		const int written = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, byteCount, &wideText[0], charCount);
		if (written != charCount)
			return {};

		return wideText;
	}

	std::string WideToUtf8Bytes(const wchar_t* text, size_t textLength)
	{
		if (text == nullptr || textLength == 0)
			return {};

		const int charCount = ClampLength(textLength);
		if (charCount <= 0)
			return {};

		const int byteCount = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, charCount, nullptr, 0, nullptr, nullptr);
		if (byteCount <= 0)
			return {};

		std::string utf8Text(static_cast<size_t>(byteCount), '\0');
		const int written = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, charCount, &utf8Text[0], byteCount, nullptr, nullptr);
		if (written != byteCount)
			return {};

		return utf8Text;
	}

#else

	// Non-Windows: wchar_t is UTF-32 (Linux, Android), so the conversion is a
	// plain code point walk with no locale or platform dependency.

	std::wstring Utf8BytesToWide(const char* text, size_t textLength)
	{
		if (text == nullptr || textLength == 0)
			return {};

		std::vector<wchar_t> wide;
		wide.reserve(textLength);

		size_t index = 0;
		while (index < textLength)
		{
			const unsigned char lead = static_cast<unsigned char>(text[index]);
			unsigned int codePoint = 0;
			size_t extraBytes = 0;
			if (lead < 0x80)
			{
				codePoint = lead;
			}
			else if ((lead & 0xE0) == 0xC0)
			{
				codePoint = lead & 0x1Fu;
				extraBytes = 1;
			}
			else if ((lead & 0xF0) == 0xE0)
			{
				codePoint = lead & 0x0Fu;
				extraBytes = 2;
			}
			else if ((lead & 0xF8) == 0xF0)
			{
				codePoint = lead & 0x07u;
				extraBytes = 3;
			}
			else
			{
				// Invalid lead byte: skip it.
				++index;
				continue;
			}

			if (index + extraBytes >= textLength)
				break;

			bool valid = true;
			for (size_t offset = 1; offset <= extraBytes; ++offset)
			{
				const unsigned char continuation = static_cast<unsigned char>(text[index + offset]);
				if ((continuation & 0xC0) != 0x80)
				{
					valid = false;
					break;
				}
				codePoint = (codePoint << 6) | (continuation & 0x3Fu);
			}

			index += extraBytes + 1;
			if (!valid)
				continue;
			wide.push_back(static_cast<wchar_t>(codePoint));
		}

		return std::wstring(wide.begin(), wide.end());
	}

	std::string WideToUtf8Bytes(const wchar_t* text, size_t textLength)
	{
		if (text == nullptr || textLength == 0)
			return {};

		std::string utf8;
		utf8.reserve(textLength);

		for (size_t i = 0; i < textLength; ++i)
		{
			unsigned int codePoint = static_cast<unsigned int>(text[i]);
			if (codePoint < 0x80)
			{
				utf8.push_back(static_cast<char>(codePoint));
			}
			else if (codePoint < 0x800)
			{
				utf8.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
				utf8.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
			}
			else if (codePoint < 0x10000)
			{
				utf8.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
				utf8.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
				utf8.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
			}
			else
			{
				utf8.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
				utf8.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
				utf8.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
				utf8.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
			}
		}

		return utf8;
	}

#endif
}

std::wstring sl_text::Utf8ToWide(const std::string& text)
{
	return Utf8BytesToWide(text.data(), text.size());
}

std::wstring sl_text::Utf8ToWide(const char* text, int length)
{
	if (text == nullptr || length <= 0)
		return {};

	return Utf8BytesToWide(text, static_cast<size_t>(length));
}

std::string sl_text::WideToUtf8(const std::wstring& text)
{
	return WideToUtf8Bytes(text.data(), text.size());
}

std::string sl_text::WideToUtf8(const wchar_t* text, int length)
{
	if (text == nullptr || length <= 0)
		return {};

	return WideToUtf8Bytes(text, static_cast<size_t>(length));
}
