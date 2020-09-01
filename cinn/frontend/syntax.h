#pragma once
/**
 * \file This file defines some basic elements of CINN frontend syntax.
 */
#include <glog/logging.h>

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "cinn/common/context.h"
#include "cinn/common/object.h"
#include "cinn/common/type.h"

namespace cinn {
namespace frontend {

struct Program;
struct Variable;

/**
 * Placeholder is the fed slot of a computation.
 */
class Placeholder {
 public:
  /**
   * @param type Type of the fed
   * @param shape Shape of the fed
   * @param id ID of the fed
   */
  Placeholder(const common::Type& type, const std::vector<int>& shape, std::string_view id = "")
      : type_(type), shape_(shape), id_(id.empty() ? common::Context::Global().NewName("placeholder") : id) {}

  const std::vector<int>& shape() const { return shape_; }

  std::string_view id() const { return id_; }

  operator Variable();

  Program* parent_program() { return parent_program_; }

 private:
  common::Type type_;
  std::string id_{};
  std::vector<int> shape_;
  Program* parent_program_{};
};

struct _Variable_ : public common::Object {
  std::string id;
  common::Type type;
  std::vector<int> shape;

  const char* type_info() const override { return __type_info__; }
  static constexpr char* __type_info__ = "cinn_frontend_variable";
};

/**
 * Variable represents the variable in a computation.
 */
struct Variable : public common::Shared<_Variable_> {
  /**
   * Constructor.
   * @param id The identifier of the variable, if null, a random ID will be assigned.
   */
  explicit Variable(std::string_view id = "") : common::Shared<_Variable_>(common::make_shared<_Variable_>()) {
    get()->id = id.empty() ? common::Context::Global().NewName("var") : id;
  }

  _Variable_* operator->() { return get(); }
  const _Variable_* operator->() const { return get(); }
};

/**
 * Data of a Instruction.
 */
struct _Instruction_ : public common::Object {
  using attr_t = std::variant<int, float, std::string, std::vector<int>, std::vector<float>, std::vector<std::string>>;

  std::string op_type;
  std::unordered_map<std::string, attr_t> attrs;
  std::vector<Variable> inputs;
  std::vector<Variable> outputs;
  Program* parent_program{};

  const char* type_info() const override { return __type_info__; }

  static constexpr char* __type_info__ = "cinn_frontend_instruction";
};

/**
 * Instruction is the basic computational unit of a Program, similar to the operator concept in a DNN platform.
 */
struct Instruction : public common::Shared<_Instruction_> {
  explicit Instruction(std::string_view op_type, Program* parent = nullptr);

  /**
   * Set the inputs of the instruction.
   * @param vars The input variables.
   */
  void SetInputs(const std::vector<Variable>& vars) { get()->inputs = vars; }
  const std::vector<Variable>& GetOutputs() const { return get()->outputs; }

  /**
   * Set an attribute of the instruction.
   * @tparam T The type of the attribute value type.
   * @param key The identifier of the attribute.
   * @param v The value of the attribute.
   */
  template <typename T>
  void SetAttr(const std::string& key, const T& v) {
    get()->attrs[key] = v;
  }

  /**
   * Get an attribute of the instruction.
   * @tparam T The data type of the attribute value.
   * @param key The identifier of the attribute.
   * @return The attribute value.
   */
  template <typename T>
  T GetAttrs(const std::string& key) {
    auto it = get()->attrs.find(key);
    CHECK(it != get()->attrs.end()) << "No attribute called [" << key << "]";
    return std::get<T>(it->second);
  }

 private:
  // Generate outputs according to op's declaration.
  void PrepareOutputs();
};

/**
 * Program is a representation of a computation.
 */
struct Program {
  /**
   * Add two variables.
   *
   * @param a The first variable.
   * @param b The second variable.
   * @return The result.
   */
  Variable add(const Variable& a, const Variable& b);

  /**
   * Get \p i-th instruction.
   */
  Instruction& operator[](size_t i);
  /**
   * Get \p i-th instruction.
   */
  const Instruction& operator[](size_t i) const;

  /**
   * Get number of instructions in the program.
   * @return
   */
  inline size_t size() const { return instrs.size(); }

 private:
  void AddInstruction(const Instruction& other) { instrs.push_back(other); }

  std::vector<Instruction> instrs;
};

std::ostream& operator<<(std::ostream& os, const Variable& x);
std::ostream& operator<<(std::ostream& os, const Instruction& instr);

}  // namespace frontend
}  // namespace cinn