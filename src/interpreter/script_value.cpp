#include "script_value.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace shell {

// ScriptValue类型转换实现
bool ScriptValue::as_bool() const {
  if (!is_bool()) {
    throw std::runtime_error("Type error: value is not a boolean");
  }
  return std::get<bool>(value_);
}

double ScriptValue::as_number() const {
  if (!is_number()) {
    throw std::runtime_error("Type error: value is not a number");
  }
  return std::get<double>(value_);
}

const std::string &ScriptValue::as_string() const {
  if (!is_string()) {
    throw std::runtime_error("Type error: value is not a string");
  }
  return std::get<std::string>(value_);
}

const std::vector<ScriptValue> &ScriptValue::as_array() const {
  if (!is_array()) {
    throw std::runtime_error("Type error: value is not an array");
  }
  return std::get<std::vector<ScriptValue>>(value_);
}

std::shared_ptr<ScriptObject> ScriptValue::as_object() const {
  if (!is_object()) {
    throw std::runtime_error("Type error: value is not an object");
  }
  return std::get<std::shared_ptr<ScriptObject>>(value_);
}

std::shared_ptr<ScriptFunction> ScriptValue::as_function() const {
  if (!is_function()) {
    throw std::runtime_error("Type error: value is not a function");
  }
  return std::get<std::shared_ptr<ScriptFunction>>(value_);
}

// 安全类型转换实现
std::optional<bool> ScriptValue::to_bool() const {
  if (is_bool()) {
    return std::get<bool>(value_);
  }

  if (is_number()) {
    double num = std::get<double>(value_);
    return num != 0 && !std::isnan(num);
  }

  if (is_string()) {
    const std::string &str = std::get<std::string>(value_);
    return !str.empty();
  }

  if (is_null()) {
    return false;
  }

  // 数组和对象总是为true
  if (is_array() || is_object() || is_function()) {
    return true;
  }

  return std::nullopt;
}

std::optional<double> ScriptValue::to_number() const {
  if (is_number()) {
    return std::get<double>(value_);
  }

  if (is_bool()) {
    return std::get<bool>(value_) ? 1.0 : 0.0;
  }

  if (is_string()) {
    const std::string &str = std::get<std::string>(value_);
    try {
      size_t pos;
      double result = std::stod(str, &pos);

      // 确保整个字符串都被解析
      if (pos == str.length()) {
        return result;
      }
    } catch (...) {
      // 解析失败
    }
  }

  if (is_null()) {
    return 0.0;
  }

  return std::nullopt;
}

std::optional<std::string> ScriptValue::to_string() const {
  if (is_string()) {
    return std::get<std::string>(value_);
  }

  // 调用to_debug_string并返回
  return to_debug_string();
}

std::optional<std::vector<ScriptValue>> ScriptValue::to_array() const {
  if (is_array()) {
    return std::get<std::vector<ScriptValue>>(value_);
  }

  if (is_string()) {
    // 将字符串转换为字符数组
    const std::string &str = std::get<std::string>(value_);
    std::vector<ScriptValue> result;
    for (char c : str) {
      result.emplace_back(std::string(1, c));
    }
    return result;
  }

  return std::nullopt;
}

std::optional<std::shared_ptr<ScriptObject>> ScriptValue::to_object() const {
  if (is_object()) {
    return std::get<std::shared_ptr<ScriptObject>>(value_);
  }
  return std::nullopt;
}

std::optional<std::shared_ptr<ScriptFunction>>
ScriptValue::to_function() const {
  if (is_function()) {
    return std::get<std::shared_ptr<ScriptFunction>>(value_);
  }
  return std::nullopt;
}

// 操作符重载实现
bool ScriptValue::operator==(const ScriptValue &other) const {
  if (value_.index() != other.value_.index()) {
    // 类型不同，需要尝试类型转换

    // 如果一方是数字，另一方可以转换为数字，则比较数值
    if ((is_number() || other.is_number()) && to_number().has_value() &&
        other.to_number().has_value()) {
      return to_number().value() == other.to_number().value();
    }

    // 如果一方是字符串，另一方可以转换为字符串，则比较字符串
    if ((is_string() || other.is_string()) && to_string().has_value() &&
        other.to_string().has_value()) {
      return to_string().value() == other.to_string().value();
    }

    // 如果一方是布尔值，另一方可以转换为布尔值，则比较布尔值
    if ((is_bool() || other.is_bool()) && to_bool().has_value() &&
        other.to_bool().has_value()) {
      return to_bool().value() == other.to_bool().value();
    }

    // 类型不兼容
    return false;
  }

  // 类型相同，直接比较
  switch (value_.index()) {
  case 0:        // null
    return true; // null只等于null
  case 1:        // bool
    return std::get<bool>(value_) == std::get<bool>(other.value_);
  case 2: // number
    return std::get<double>(value_) == std::get<double>(other.value_);
  case 3: // string
    return std::get<std::string>(value_) == std::get<std::string>(other.value_);
  case 4: { // array
    const auto &arr1 = std::get<std::vector<ScriptValue>>(value_);
    const auto &arr2 = std::get<std::vector<ScriptValue>>(other.value_);

    if (arr1.size() != arr2.size()) {
      return false;
    }

    for (size_t i = 0; i < arr1.size(); ++i) {
      if (arr1[i] != arr2[i]) {
        return false;
      }
    }

    return true;
  }
  case 5: // object
    return std::get<std::shared_ptr<ScriptObject>>(value_) ==
           std::get<std::shared_ptr<ScriptObject>>(other.value_);
  case 6: // function
    return std::get<std::shared_ptr<ScriptFunction>>(value_) ==
           std::get<std::shared_ptr<ScriptFunction>>(other.value_);
  default:
    return false;
  }
}

// 格式化值为调试字符串
std::string ScriptValue::to_debug_string() const {
  std::ostringstream oss;

  switch (value_.index()) {
  case 0: // null
    oss << "null";
    break;
  case 1: // bool
    oss << (std::get<bool>(value_) ? "true" : "false");
    break;
  case 2: { // number
    double num = std::get<double>(value_);

    // 检查是否是整数
    if (std::floor(num) == num) {
      oss << static_cast<long long>(num);
    } else {
      oss << std::setprecision(15) << num;
    }
    break;
  }
  case 3: // string
    oss << "\"" << std::get<std::string>(value_) << "\"";
    break;
  case 4: { // array
    const auto &arr = std::get<std::vector<ScriptValue>>(value_);
    oss << "[";
    for (size_t i = 0; i < arr.size(); ++i) {
      if (i > 0)
        oss << ", ";
      oss << arr[i].to_debug_string();
    }
    oss << "]";
    break;
  }
  case 5: { // object
    auto obj = std::get<std::shared_ptr<ScriptObject>>(value_);
    oss << "[Object " << obj->get_type_name() << "]";
    break;
  }
  case 6: { // function
    auto func = std::get<std::shared_ptr<ScriptFunction>>(value_);
    oss << "[Function: " << func->get_name() << "]";
    break;
  }
  default:
    oss << "[Unknown]";
  }

  return oss.str();
}

// ScriptObject实现
void ScriptObject::set_property(const std::string &name, ScriptValue value) {
  properties_[name] = std::move(value);
}

ScriptValue ScriptObject::get_property(const std::string &name) const {
  auto it = properties_.find(name);
  if (it != properties_.end()) {
    return it->second;
  }
  return ScriptValue(nullptr); // 属性不存在返回null
}

bool ScriptObject::has_property(const std::string &name) const {
  return properties_.find(name) != properties_.end();
}

ScriptValue ScriptObject::call_method(const std::string &name,
                                      const std::vector<ScriptValue> &args) {
  auto it = properties_.find(name);
  if (it != properties_.end() && it->second.is_function()) {
    return it->second.as_function()->call(args);
  }
  throw std::runtime_error("Method not found: " + name);
}

std::unordered_map<std::string, ScriptValue>
ScriptObject::get_all_properties() const {
  return properties_;
}

} // namespace shell