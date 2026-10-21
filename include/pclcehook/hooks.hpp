#pragma once

namespace pclcehook::hooks {

/**
 * 安装并启用全部伪造 hook。
 *
 * 覆盖 PCL-CE v2.15.1 `RegionPolicy.ReadWindowsSignals()` 读取的所有系统信号，
 * 使地区判定落到 `RegionClassification.Other`（新加坡）：
 *  - GetUserDefaultGeoName        -> "SG"
 *  - RegQueryValueExW(DeviceRegion) -> REG_DWORD 215 (0xD7)
 *  - GetDynamicTimeZoneInformation -> "Singapore Standard Time"
 *  - GetTimeZoneInformation        -> "Singapore Standard Time"
 *
 * 用户区域（GetUserDefaultLocaleName）、用户首选 UI 语言列表（GetUserPreferredUILanguages）
 * 保持系统原值，因此 zh-CN 与代码页行为不变。
 *
 * @return 成功启用的 hook 数量；-1 表示 MH_Initialize 失败，-2 表示没有任何 hook 安装成功。
 * @note 必须在目标进程进入托管代码之前调用：.NET 的 TimeZoneInfo.Local 在首次访问后
 *       由静态缓存持有，晚于该时刻注入会让时区信号保留真实值。
 */
int Install() noexcept;

/** 返回已启用的 hook 数量。 */
int InstalledCount() noexcept;

}  // namespace pclcehook::hooks
