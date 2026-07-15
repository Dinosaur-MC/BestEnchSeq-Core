# I/O 层（`io/`）

自包含的 I/O 工具集，负责 JSON 解析、CSV 读写、二进制流操作。

---

## JSON（`json.h/.cpp`）

自定义 JSON 库，零外部依赖。

```cpp
enum class JsonType { Empty, Null, Bool, Number, String, Array, Object };

using Number = std::variant<int32_t, int64_t, float, double>;
using Array  = std::vector<Json>;
using Object = std::unordered_map<std::string, Json>;
using Value  = std::variant<Null, Bool, Number, String, Array, Object>;

class Json {
    // 构造
    Json() = default;
    Json(const Json&);
    Json(Json&&);
    // 类型查询
    JsonType type() const;
    JsonType type(const std::string& path) const;  // 路径式类型查询
    bool is_valid() const;

    // 值访问
    Value& get_value();
    const Value& get_value() const;

    // 序列化
    std::string to_string(Style style = Compact) const;  // Compact / Pretty

    // 静态工厂
    static Json null();
    static Json parse(std::string_view str);
    static Json parse(std::string_view str, std::string* error);  // 带错误信息
    static Json parse(std::istream& is);
    static Json parse(std::istream& is, std::string* error);

    // 运算符
    bool operator==(const Json& other) const;
};
```

特点：
- 递归下降解析，支持嵌套对象和数组
- 路径式访问（`type("data.equipment.sword")`）
- 两种序列化风格：Compact（无空格）和 Pretty（缩进）
- `Number` 使用 `variant` 区分 int32 / int64 / float / double

---

## CsvIO（`CsvIO.h/.cpp`）

CSV 读写工具。

```cpp
class CsvIO {
    // 读取 CSV 文件
    static std::vector<std::vector<std::string>> read(
        const std::string& path, char delim = ',');

    // 写入 CSV 文件
    static bool write(const std::string& path,
                      const std::vector<std::vector<std::string>>& rows,
                      char delim = ',');

    // 便捷写入（带表头）
    static bool write_with_header(
        const std::string& path,
        const std::vector<std::string>& headers,
        const std::vector<std::vector<std::string>>& rows,
        char delim = ',');
};
```

用于附魔/装备数据的 CSV 导入导出。`EnchSerializer` 调用此接口生成数据文件。

---

## ByteStream（`ByteStream.h`）

二进制流读写工具。

```cpp
class ByteStream {
    explicit ByteStream(size_t initial_capacity);
    explicit ByteStream(const void* data, size_t size);

    // 写入
    template <typename T>
    void write(const T& value);
    template <typename T>
    void write_array(const T* data, size_t count);
    void write_string(std::string_view s);

    // 读取
    template <typename T>
    T read();
    template <typename T>
    void read_array(T* out, size_t count);
    std::string read_string(size_t length);

    // 定位
    size_t tell() const;
    void seek(size_t pos);
    void skip(size_t bytes);

    // 底层访问
    const uint8_t* data() const;
    size_t size() const;
    bool empty() const;
};
```

用于序列化附魔数据和 AStar 搜索状态的存档/恢复。

---

## 开发说明

- JSON 库是手写的递归下降解析器，无 rapidjson / nlohmann 依赖
- CsvIO 支持自定义分隔符，默认逗号
- ByteStream 不自动处理字节序（不涉及跨平台文件交换）
