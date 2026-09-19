// AsceRowNameWrapperCustomization.h
#pragma once

#include "IPropertyTypeCustomization.h"

/**
 * `FAsceRowNameWrapper` 的属性类型定制：
 * 按属性 meta `DataTableBinding="<别名>[:<字段1>,<字段2>...]"` 渲染"行名下拉"。
 *
 * - 别名 = `UAsceDataTableSettings` 变量名（编辑器里也能解析：子系统是引擎子系统）；
 * - 字段 = 行结构属性名，仅用于下拉项里显示对应值（如 `Op=Wire  ThroughputPerTick=1`）；
 * - 每次打开菜单时懒重建（GameFeature 挂/卸 SDT 后不重启也能看到新行）。
 */
class FAsceRowNameWrapperCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ Begin IPropertyTypeCustomization Interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}
	//~ End IPropertyTypeCustomization Interface
};
