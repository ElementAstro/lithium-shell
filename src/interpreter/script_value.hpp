#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace shell {

// Forward declarations
class ScriptFunction;
class ScriptObject;

/**
 * @class ScriptValue
 * @brief Represents a value in a script, supporting multiple types
 *
 * A type-safe representation of script values implemented using std::variant.
 * Supports common data types and custom object types used in scripting
 * languages.
 */
class ScriptValue {
public:
  // Constructors for various value types
  ScriptValue() : value_(nullptr) {} ///< Creates a null value
  ScriptValue(std::nullptr_t) : value_(nullptr) {}
  ScriptValue(bool value) : value_(value) {}
  ScriptValue(int value) : value_(static_cast<double>(value)) {}
  ScriptValue(double value) : value_(value) {}
  ScriptValue(const char *value) : value_(std::string(value)) {}
  ScriptValue(std::string value) : value_(std::move(value)) {}

  // Array and object constructors
  ScriptValue(std::vector<ScriptValue> values) : value_(std::move(values)) {}
  ScriptValue(std::shared_ptr<ScriptObject> object)
      : value_(std::move(object)) {}
  ScriptValue(std::shared_ptr<ScriptFunction> function)
      : value_(std::move(function)) {}

  /**
   * @brief Checks if the value is null
   * @return True if the value is null, false otherwise
   */
  bool is_null() const {
    return std::holds_alternative<std::nullptr_t>(value_);
  }

  /**
   * @brief Checks if the value is a boolean
   * @return True if the value is a boolean, false otherwise
   */
  bool is_bool() const { return std::holds_alternative<bool>(value_); }

  /**
   * @brief Checks if the value is a number
   * @return True if the value is a number, false otherwise
   */
  bool is_number() const { return std::holds_alternative<double>(value_); }

  /**
   * @brief Checks if the value is a string
   * @return True if the value is a string, false otherwise
   */
  bool is_string() const { return std::holds_alternative<std::string>(value_); }

  /**
   * @brief Checks if the value is an array
   * @return True if the value is an array, false otherwise
   */
  bool is_array() const {
    return std::holds_alternative<std::vector<ScriptValue>>(value_);
  }

  /**
   * @brief Checks if the value is an object
   * @return True if the value is an object, false otherwise
   */
  bool is_object() const {
    return std::holds_alternative<std::shared_ptr<ScriptObject>>(value_);
  }

  /**
   * @brief Checks if the value is a function
   * @return True if the value is a function, false otherwise
   */
  bool is_function() const {
    return std::holds_alternative<std::shared_ptr<ScriptFunction>>(value_);
  }

  /**
   * @brief Converts the value to boolean (with type checking)
   * @return The boolean value
   * @throws std::bad_variant_access if the value is not a boolean
   */
  bool as_bool() const;

  /**
   * @brief Converts the value to a number (with type checking)
   * @return The numeric value
   * @throws std::bad_variant_access if the value is not a number
   */
  double as_number() const;

  /**
   * @brief Gets the string value (with type checking)
   * @return Reference to the string value
   * @throws std::bad_variant_access if the value is not a string
   */
  const std::string &as_string() const;

  /**
   * @brief Gets the array value (with type checking)
   * @return Reference to the array value
   * @throws std::bad_variant_access if the value is not an array
   */
  const std::vector<ScriptValue> &as_array() const;

  /**
   * @brief Gets the object value (with type checking)
   * @return Shared pointer to the object
   * @throws std::bad_variant_access if the value is not an object
   */
  std::shared_ptr<ScriptObject> as_object() const;

  /**
   * @brief Gets the function value (with type checking)
   * @return Shared pointer to the function
   * @throws std::bad_variant_access if the value is not a function
   */
  std::shared_ptr<ScriptFunction> as_function() const;

  /**
   * @brief Safely converts the value to boolean
   * @return An optional containing the boolean value if conversion is possible,
   * empty otherwise
   */
  std::optional<bool> to_bool() const;

  /**
   * @brief Safely converts the value to a number
   * @return An optional containing the numeric value if conversion is possible,
   * empty otherwise
   */
  std::optional<double> to_number() const;

  /**
   * @brief Safely converts the value to a string
   * @return An optional containing the string value if conversion is possible,
   * empty otherwise
   */
  std::optional<std::string> to_string() const;

  /**
   * @brief Safely converts the value to an array
   * @return An optional containing the array value if conversion is possible,
   * empty otherwise
   */
  std::optional<std::vector<ScriptValue>> to_array() const;

  /**
   * @brief Safely converts the value to an object
   * @return An optional containing the object if conversion is possible, empty
   * otherwise
   */
  std::optional<std::shared_ptr<ScriptObject>> to_object() const;

  /**
   * @brief Safely converts the value to a function
   * @return An optional containing the function if conversion is possible,
   * empty otherwise
   */
  std::optional<std::shared_ptr<ScriptFunction>> to_function() const;

  /**
   * @brief Equality comparison operator
   * @param other The ScriptValue to compare with
   * @return True if values are equal, false otherwise
   */
  bool operator==(const ScriptValue &other) const;

  /**
   * @brief Inequality comparison operator
   * @param other The ScriptValue to compare with
   * @return True if values are not equal, false otherwise
   */
  bool operator!=(const ScriptValue &other) const { return !(*this == other); }

  /**
   * @brief Converts the value to a human-readable string representation
   * @return A string representation suitable for debugging
   */
  std::string to_debug_string() const;

private:
  /** The internal variant storing the actual value */
  std::variant<std::nullptr_t, bool, double, std::string,
               std::vector<ScriptValue>, std::shared_ptr<ScriptObject>,
               std::shared_ptr<ScriptFunction>>
      value_;
};

/**
 * @class ScriptObject
 * @brief Represents an object in a script with properties and methods
 *
 * Base class for all script objects that can have properties and methods.
 * Implements common functionality for property access and method calls.
 */
class ScriptObject : public std::enable_shared_from_this<ScriptObject> {
public:
  /**
   * @brief Virtual destructor to ensure proper cleanup of derived classes
   */
  virtual ~ScriptObject() = default;

  /**
   * @brief Sets a property value on the object
   * @param name The property name
   * @param value The value to set
   */
  virtual void set_property(const std::string &name, ScriptValue value);

  /**
   * @brief Gets a property value from the object
   * @param name The property name
   * @return The property value
   */
  virtual ScriptValue get_property(const std::string &name) const;

  /**
   * @brief Checks if a property exists on the object
   * @param name The property name
   * @return True if the property exists, false otherwise
   */
  virtual bool has_property(const std::string &name) const;

  /**
   * @brief Calls a method on the object
   * @param name The method name
   * @param args The arguments to pass to the method
   * @return The return value from the method call
   */
  virtual ScriptValue call_method(const std::string &name,
                                  const std::vector<ScriptValue> &args);

  /**
   * @brief Gets all properties of the object
   * @return Map of property names to their values
   */
  virtual std::unordered_map<std::string, ScriptValue>
  get_all_properties() const;

  /**
   * @brief Gets the type name of the object
   * @return String representation of the object's type
   */
  virtual std::string get_type_name() const { return "Object"; }

protected:
  /** Map of property names to their values */
  std::unordered_map<std::string, ScriptValue> properties_;
};

/**
 * @class ScriptFunction
 * @brief Represents a callable function in the script environment
 *
 * Encapsulates a callable function that can be executed from the script.
 * Can be a native C++ function or a script-defined function.
 */
class ScriptFunction {
public:
  /** Type alias for native function implementations */
  using NativeFunction =
      std::function<ScriptValue(const std::vector<ScriptValue> &)>;

  /**
   * @brief Constructor for a function
   * @param func The native C++ function to wrap
   * @param name The function name (defaults to "<anonymous>")
   */
  ScriptFunction(NativeFunction func, std::string name = "<anonymous>")
      : native_func_(std::move(func)), name_(std::move(name)) {}

  /**
   * @brief Virtual destructor to ensure proper cleanup of derived classes
   */
  virtual ~ScriptFunction() = default;

  /**
   * @brief Calls the function with the given arguments
   * @param args The arguments to pass to the function
   * @return The return value from the function call
   */
  virtual ScriptValue call(const std::vector<ScriptValue> &args) {
    return native_func_(args);
  }

  /**
   * @brief Gets the name of the function
   * @return The function name
   */
  const std::string &get_name() const { return name_; }

private:
  /** The native function implementation */
  NativeFunction native_func_;

  /** The function name */
  std::string name_;
};

} // namespace shell