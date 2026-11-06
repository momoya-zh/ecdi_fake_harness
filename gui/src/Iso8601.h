#pragma once
// ISO-8601 UTC 毫秒时刻的解析（协议 §1 「ts」字段）。
//
// ★★ 2026-11-06 抽出：这段算法原本只活在 `ReplaySource.cpp` 的匿名 namespace 里，
//   用途单一（算回放节奏的相对偏移）。回合折叠要显示「· 1.2s」就必须从**块自己**的
//   `ts` 求差，于是同一个算法有了第二个消费者。
//   ⇒ 复制一份会立刻产生两处「纪元/闰年/月份折半」的独立实现，而它们的错误表现
//     恰好都是**静默的**（时间差偏一天、偏一秒），没有任何探针会红。
//   故抽成头文件同一份实现，两边共用。
//
// ★ 为什么口径是「任意纪元」而不是 Unix epoch：唯一用途是**求差**（回放节奏、
//   块间耗时），绝对时刻从不出现在屏幕上。选一个固定纪元（`DaysFromCivil` 的
//   1970-01-01）只是让差值的单位仍是毫秒，不引入任何时区换算。
//
// ★ 只接受协议规定的形状 `YYYY-MM-DDTHH:MM:SS.mmmZ`（§1：core→gui 必填、UTC、
//   毫秒）。**不做宽容解析**：容忍 `+08:00` 偏移或省略毫秒会让"解析成功"这件事
//   失去判据，而调用方（回放节奏）对错误值的反应是**静默**的。
//   ⇒ 不匹配即返回 false，调用方自己决定降级（回放：偏移记 0；折叠：不显示耗时）。

#include <cstdint>
#include <cstdio>
#include <string>

namespace harness::iso8601 {

// Howard Hinnant 的 days_from_civil：civil 日期 → 自 1970-01-01 的天数。
// 不依赖本地时区 API（`mktime` 会按本地时区解释，回放节奏就会随机器时区漂）。
inline int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// "YYYY-MM-DDTHH:MM:SS.mmmZ" → 自 1970-01-01T00:00:00.000Z 的毫秒数。
// 形状不符 ⇒ 返回 false（**不**部分填充 outMs）。
inline bool ParseIso8601Ms(const std::string& text, int64_t& outMs) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, ms = 0;
    if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d.%3dZ",
                    &y, &mo, &d, &h, &mi, &s, &ms) != 7) {
        return false;
    }
    const int64_t days =
        DaysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d));
    outMs = ((days * 86400 + h * 3600 + mi * 60 + s) * 1000) + ms;
    return true;
}

}   // namespace harness::iso8601
