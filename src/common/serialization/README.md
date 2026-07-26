# 序列化接口层（`src/common/serialization/`）

定义序列化通用根接口，由业务域类型实现。

## 接口层级

```
ISerializable          ← 格式无关基类（virtual ~ISerializable()）
  ├── IJsonSerializable   ← JSON 序列化（to_json() / from_json()）
  └── IBinarySerializable ← 二进制序列化（serialize() / deserialize()）
```

## 使用方式

业务域类型（`EnchInfo`、`Equipment` 等）继承 `IJsonSerializable` 并实现 `to_json()` / `from_json()`。

`Serializer.h`（在 `domain/business/components/`）提供 ADL 兼容的自由函数委托：

```cpp
Json& operator<<(Json& json, const EnchInfo& info);  // 委托 info.to_json()
void operator>>(const Json& json, EnchInfo& info);    // 委托 info.from_json(json)
```

## 模板辅助函数

```cpp
#include "common/serialization/IJsonSerializable.h"

auto j = json::serialize(obj);                       // → obj.to_json()
auto obj = json::deserialize<MyType>(json);           // → construct + from_json
json::deserialize(existing_obj, json);                // → existing.from_json()
auto arr = json::serialize_vector(vec);               // → array of to_json()
```
