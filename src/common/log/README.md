# 日志层（`src/common/log/`）

全局异步日志系统，基于 `std::atomic::wait` 实现零 CPU 空闲等待。

---

## Logger

Meyer's 单例，所有日志操作非阻塞。

```cpp
class Logger {
    static Logger& instance();

    // 日志
    void log(LogLevel level, std::string_view message);
    void info(std::string_view msg);
    void warn(std::string_view msg);
    void error(std::string_view msg);
    void debug(std::string_view msg);

    // printf 风格格式化
    void info_fmt(const char* fmt, ...);
    void warn_fmt(const char* fmt, ...);
    void error_fmt(const char* fmt, ...);
    void debug_fmt(const char* fmt, ...);
    void printf(LogLevel level, const char* fmt, ...);

    // 控制
    void flush();                         // 同步刷新待处理日志
    void set_level(LogLevel lv);
    LogLevel get_level() const;
    void set_retention(size_t n);         // 保留的最新日志文件数
    size_t get_retention() const;
};
```

## 日志级别

```cpp
enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
    None
};
```

## 日志宏（`log.hpp`）

```cpp
#define LOG_DEBUG(...)   Logger::instance().debug_fmt(__VA_ARGS__)
#define LOG_INFO(...)    Logger::instance().info_fmt(__VA_ARGS__)
#define LOG_WARN(...)    Logger::instance().warn_fmt(__VA_ARGS__)
#define LOG_ERROR(...)   Logger::instance().error_fmt(__VA_ARGS__)
```

## 架构

```
LOG_INFO("processing %d items", n)
  │
  ▼
Logger::info_fmt()          ← printf 格式化
  │
  ▼
Logger::log(Info, msg)      ← push 到无界队列（非阻塞）
  │
  ▼
SegmentedMPSCQueue           ← 生产者：任意线程
  │
  ▼
后台消费线程                  ← atomic::wait（零 CPU 空闲）
  │
  ▼
文件写入 + 轮转               ← set_retention 控制保留数
```

特点：
- 日志队列使用 `SegmentedMPSCQueue`，无界、优雅处理尖峰
- 消费线程在队列空时通过 `std::atomic::wait` 挂起，不消耗 CPU
- `flush()` 同步等待消费线程处理完所有待办日志
- 运行时可通过 `set_level()` 控制日志级别，`Debug` 级别日志在生成侧过滤

## 全局依赖

Logger 是项目级别的全局基础设施，被所有层使用（registries、algorithm、parsers 等）。
在 `tests/CMakeLists.txt` 中以 `GLOBAL_LOGGER_SRCS` 变量统一引用。
