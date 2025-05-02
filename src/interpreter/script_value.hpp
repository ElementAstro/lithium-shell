#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace shell {

// 前向声明
class ScriptFunction;
class ScriptObject;

/**
 * @class ScriptValue
 * @brief 表示脚本中的值，支持多种类型
 *
 * 使用std::variant实现的类型安全的脚本值表示，支持常见的数据类型
 * 和自定义对象类型。
 */
class ScriptValue {
public:
  // 构造函数 - 各种类型的值
  ScriptValue() : value_(nullptr) {} // null值
  ScriptValue(std::nullptr_t) : value_(nullptr) {}
  ScriptValue(bool value) : value_(value) {}
  ScriptValue(int value) : value_(static_cast<double>(value)) {}
  ScriptValue(double value) : value_(value) {}
  ScriptValue(const char *value) : value_(std::string(value)) {}
  ScriptValue(std::string value) : value_(std::move(value)) {}

  // 数组和对象构造
  ScriptValue(std::vector<ScriptValue> values) : value_(std::move(values)) {}
  ScriptValue(std::shared_ptr<ScriptObject> object)
      : value_(std::move(object)) {}
  ScriptValue(std::shared_ptr<ScriptFunction> function)
      : value_(std::move(function)) {}

  // 类型检查
  bool is_null() const {
    return std::holds_alternative<std::nullptr_t>(value_);
  }
  bool is_bool() const { return std::holds_alternative<bool>(value_); }
  bool is_number() const { return std::holds_alternative<double>(value_); }
  bool is_string() const { return std::holds_alternative<std::string>(value_); }
  bool is_array() const {
    return std::holds_alternative<std::vector<ScriptValue>>(value_);
  }
  bool is_object() const {
    return std::holds_alternative<std::shared_ptr<ScriptObject>>(value_);
  }
  bool is_function() const {
    return std::holds_alternative<std::shared_ptr<ScriptFunction>>(value_);
  }

  // 类型转换 (带类型检查)
  bool as_bool() const;
  double as_number() const;
  const std::string &as_string() const;
  const std::vector<ScriptValue> &as_array() const;
  std::shared_ptr<ScriptObject> as_object() const;
  std::shared_ptr<ScriptFunction> as_function() const;

  // 安全类型转换，返回std::optional
  std::optional<bool> to_bool() const;
  std::optional<double> to_number() const;
  std::optional<std::string> to_string() const;
  std::optional<std::vector<ScriptValue>> to_array() const;
  std::optional<std::shared_ptr<ScriptObject>> to_object() const;
  std::optional<std::shared_ptr<ScriptFunction>> to_function() const;

  // 操作符重载
  bool operator==(const ScriptValue &other) const;
  bool operator!=(const ScriptValue &other) const { return !(*this == other); }

  // 格式化为字符串
  std::string to_debug_string() const;

private:
  std::variant<std::nullptr_t, bool, double, std::string,
               std::vector<ScriptValue>, std::shared_ptr<ScriptObject>,
               std::shared_ptr<ScriptFunction>>
      value_;
};

/**
 * @class ScriptObject
 * @brief 表示脚本中的对象，具有属性和方法
 */
class ScriptObject : public std::enable_shared_from_this<ScriptObject> {
public:
  virtual ~ScriptObject() = default;

  // 属性操作
  virtual void set_property(const std::string &name, ScriptValue value);
  virtual ScriptValue get_property(const std::string &name) const;
  virtual bool has_property(const std::string &name) const;

  // 方法调用
  virtual ScriptValue call_method(const std::string &name,
                                  const std::vector<ScriptValue> &args);

  // 获取所有属性
  virtual std::unordered_map<std::string, ScriptValue>
  get_all_properties() const;

  // 类型信息
  virtual std::string get_type_name() const { return "Object"; }

protected:
  std::unordered_map<std::string, ScriptValue> properties_;
};

/**
 * @class ScriptFunction
 * @brief 表示可调用的脚本函数
 */
class ScriptFunction {
public:
  using NativeFunction =
      std::function<ScriptValue(const std::vector<ScriptValue> &)>;

  ScriptFunction(NativeFunction func, std::string name = "<anonymous>")
      : native_func_(std::move(func)), name_(std::move(name)) {}

  virtual ~ScriptFunction() = default;

  // 调用函数
  virtual ScriptValue call(const std::vector<ScriptValue> &args) {
    return native_func_(args);
  }

  // 获取函数名
  const std::string &get_name() const { return name_; }

private:
  NativeFunction native_func_;
  std::string name_;
};

} // namespace shell