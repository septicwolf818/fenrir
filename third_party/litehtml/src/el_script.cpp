#include "el_script.h"
#include "document.h"

#include <algorithm>
#include <cctype>


litehtml::el_script::el_script(const std::shared_ptr<document>& doc) : element(doc)
{

}

void litehtml::el_script::parse_attributes()
{
	// Script attributes are consumed by embedders that provide a script runtime.
}

void litehtml::el_script::set_attr(const char* name, const char* value)
{
	if (!name || !value) return;
	string key = name;
	std::transform(key.begin(), key.end(), key.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	m_attributes[key] = value;
}

const char* litehtml::el_script::get_attr(const char* name, const char* def) const
{
	if (!name) return def;
	string key = name;
	std::transform(key.begin(), key.end(), key.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	auto found = m_attributes.find(key);
	return found == m_attributes.end() ? def : found->second.c_str();
}

bool litehtml::el_script::appendChild(const ptr &el)
{
	el->get_text(m_text);
	return true;
}

litehtml::string_id litehtml::el_script::tag() const
{
	return _script_;
}

const char* litehtml::el_script::get_tagName() const
{
	return "script";
}
