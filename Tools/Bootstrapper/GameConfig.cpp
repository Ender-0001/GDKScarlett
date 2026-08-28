#define GDKS_TRACE_TAG "gdks"
#include "Common.h"
#include "GameConfig.h"

#include <msxml6.h>

#pragma comment(lib, "ole32.lib")

static std::wstring GetAttribute(IXMLDOMElement* element, const wchar_t* name)
{
	std::wstring result;
	if (!element)
	{
		return result;
	}
	BSTR attributeName = SysAllocString(name);
	VARIANT value;
	VariantInit(&value);
	if (element->getAttribute(attributeName, &value) == S_OK && value.vt == VT_BSTR && value.bstrVal)
	{
		result = value.bstrVal;
	}
	VariantClear(&value);
	SysFreeString(attributeName);
	return result;
}

static std::wstring GetElementText(IXMLDOMElement* element)
{
	std::wstring result;
	if (!element)
	{
		return result;
	}
	BSTR text = nullptr;
	if (element->get_text(&text) == S_OK && text)
	{
		result = text;
	}
	SysFreeString(text);
	return result;
}

static IXMLDOMElement* FirstElement(IXMLDOMDocument* document, const wchar_t* localName)
{
	std::wstring query = L"//*[local-name()='";
	query += localName;
	query += L"']";
	BSTR bstrQuery = SysAllocString(query.c_str());
	IXMLDOMNode* node = nullptr;
	IXMLDOMElement* element = nullptr;
	if (document->selectSingleNode(bstrQuery, &node) == S_OK && node)
	{
		node->QueryInterface(IID_PPV_ARGS(&element));
		node->Release();
	}
	SysFreeString(bstrQuery);
	return element;
}

bool ParseGameConfig(const std::wstring& configPath, PackageInfo& info)
{
	IXMLDOMDocument* document = nullptr;
	if (FAILED(CoCreateInstance(__uuidof(DOMDocument60), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&document))))
	{
		LOGF("could not create an XML document");
		return false;
	}
	document->put_async(VARIANT_FALSE);

	VARIANT source;
	VariantInit(&source);
	source.vt = VT_BSTR;
	source.bstrVal = SysAllocString(configPath.c_str());
	VARIANT_BOOL loaded = VARIANT_FALSE;
	HRESULT hr = document->load(source, &loaded);
	VariantClear(&source);
	if (FAILED(hr) || loaded != VARIANT_TRUE)
	{
		LOGF("could not load config: %ls", configPath.c_str());
		document->Release();
		return false;
	}

	IXMLDOMElement* executable = FirstElement(document, L"Executable");
	IXMLDOMElement* identity = FirstElement(document, L"Identity");
	IXMLDOMElement* titleId = FirstElement(document, L"TitleId");
	info.executable = GetAttribute(executable, L"Name");
	info.applicationId = GetAttribute(executable, L"Id");
	info.identityName = GetAttribute(identity, L"Name");
	info.titleId = GetElementText(titleId);
	if (executable)
	{
		executable->Release();
	}
	if (identity)
	{
		identity->Release();
	}
	if (titleId)
	{
		titleId->Release();
	}
	document->Release();

	if (info.executable.empty())
	{
		LOGF("config has no Executable");
		return false;
	}
	return true;
}
