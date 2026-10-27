#ifndef LH_EL_SCRIPT_H
#define LH_EL_SCRIPT_H

#include "element.h"

namespace litehtml
{
	class el_script : public element
	{
		string m_text;
	public:
		explicit el_script(const std::shared_ptr<document>& doc);

		void parse_attributes() override;
		bool appendChild(const ptr &el) override;
		void set_attr(const char* name, const char* value) override;
		const char* get_attr(const char* name, const char* def = nullptr) const override;
		const string& script_text() const { return m_text; }
		string_id tag() const override;
		const char* get_tagName() const override;

	private:
		string_map m_attributes;
	};
}

#endif  // LH_EL_SCRIPT_H
