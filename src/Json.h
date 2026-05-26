#pragma once

#include <map>
#include <string>
#include <vector>

namespace musuka {

class JsonValue {
public:
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;

    JsonValue();

    static JsonValue Null();
    static JsonValue Bool(bool value);
    static JsonValue Number(double value);
    static JsonValue String(std::string value);
    static JsonValue ArrayValue(Array value = {});
    static JsonValue ObjectValue(Object value = {});

    Type type() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBool() const { return type_ == Type::Bool; }
    bool IsNumber() const { return type_ == Type::Number; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    bool AsBool(bool fallback = false) const;
    double AsNumber(double fallback = 0.0) const;
    const std::string& AsString() const;
    std::string AsStringOr(std::string fallback) const;

    const Array& AsArray() const;
    Array& AsArray();
    const Object& AsObject() const;
    Object& AsObject();

    bool Has(const std::string& key) const;
    const JsonValue& At(const std::string& key) const;
    JsonValue& operator[](const std::string& key);

private:
    Type type_ = Type::Null;
    bool boolValue_ = false;
    double numberValue_ = 0.0;
    std::string stringValue_;
    Array arrayValue_;
    Object objectValue_;
};

bool ParseJson(const std::string& text, JsonValue& outValue, std::string& outError);
std::string StringifyJson(const JsonValue& value, int indent = 0);

} // namespace musuka

