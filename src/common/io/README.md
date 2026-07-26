# I/O 层（`src/common/io/`）

自包含的 I/O 工具集，负责 JSON 解析、CSV 读写、二进制流操作。

---

## JSON（`json.h/.cpp`）

自定义 JSON 库，零外部依赖。递归下降解析，支持嵌套对象和数组。
- `Json::parse(str)` / `Json::parse(str, error)` — 解析
- `to_string(Style)` — 序列化（Compact / Pretty）
- `type()` / `type(path)` — 类型查询
- Number 使用 variant 区分 int32 / int64 / float / double

---

## CsvIO（`CsvIO.h/.cpp`）

CSV 读写工具。支持自定义分隔符，默认逗号。用于附魔/装备数据的 CSV 导入导出。

---

## ByteStream（`ByteStream.h`）

二进制流读写工具。用于序列化附魔数据和 AStar 搜索状态的存档/恢复。不自动处理字节序。

---

## FileUtils（`FileUtils.hpp`）

文件操作工具。提供跨平台的文件读取/写入（`read_file` / `write_file`）、路径操作等。

---

## 开发说明

- JSON 库是手写的递归下降解析器，无 rapidjson / nlohmann 依赖
- CsvIO 支持自定义分隔符，默认逗号
- ByteStream 不自动处理字节序（不涉及跨平台文件交换）
