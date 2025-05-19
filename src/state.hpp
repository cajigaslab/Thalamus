#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <util.hpp>
#include <variant>
#include <vector>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <boost/json.hpp>
#include <boost/signals2.hpp>

#include <absl/strings/str_split.h>

#include <absl/strings/numbers.h>
#include <absl/strings/str_format.h>
#include <absl/strings/str_join.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace thalamus {
using namespace std::placeholders;
class ObservableCollection;
class ObservableDict;
class ObservableList;
using ObservableDictPtr = std::shared_ptr<ObservableDict>;
using ObservableListPtr = std::shared_ptr<ObservableList>;
using StateDict = ObservableDict;
using StateList = ObservableList;
using StateColl = ObservableCollection;

class ObservableCollection {
public:
  using Key = std::variant<std::monostate, long long int, bool, std::string>;
  using Value = std::variant<std::monostate, long long int, double, bool,
                             std::string, std::shared_ptr<ObservableDict>,
                             std::shared_ptr<ObservableList>>;
  using Map = thalamus::map<Key, Value>;
  using Vector = thalamus::vector<Value>;
  enum class Action { Set, Delete };
  using Observer = std::function<void(Action, const Key &, Value &)>;
  ObservableCollection *parent;
  std::function<bool(Action, const std::string &, ObservableCollection::Value,
                     std::function<void()>)>
      remote_storage;

  using Changed = boost::signals2::signal<void(Action, const Key &, Value &)>;
  Changed changed;
  using RecursiveChanged = boost::signals2::signal<void(
      ObservableCollection *, Action, const Key &, Value &)>;
  RecursiveChanged recursive_changed;
  class ValueWrapper {
    Key key;
    std::function<Value &()> get_value;
    std::function<bool()> has_value;
    ObservableCollection *collection;

  public:
    ValueWrapper(const Key &key, std::function<Value &()> get_value,
                 std::function<bool()> has_value,
                 ObservableCollection *collection);

    void assign(const Value &new_value,
                std::function<void()> callback = nullptr,
                bool from_remote = false);

    operator ObservableDictPtr();
    operator ObservableListPtr();
    operator long long int();
    operator unsigned long long int();
    operator unsigned long();
    operator double();
    operator bool();
    operator std::string();
    operator Value();
    Value get();
    bool operator==(const Value &other);
  };

  class VectorIteratorWrapper {
    size_t key;
    Vector::iterator iterator;
    Vector::iterator end;
    ObservableCollection *collection;
    std::optional<ValueWrapper> value_wrapper;
    friend ObservableList;
    friend ObservableDict;

  public:
    VectorIteratorWrapper();
    VectorIteratorWrapper(size_t key, Vector::iterator iterator,
                          Vector::iterator end,
                          ObservableCollection *collection);
    ValueWrapper& operator*();
    VectorIteratorWrapper &operator+(size_t count);
    VectorIteratorWrapper &operator+=(size_t count);
    VectorIteratorWrapper &operator++();
    VectorIteratorWrapper operator++(int);
    VectorIteratorWrapper &operator-(size_t count);
    VectorIteratorWrapper &operator-=(size_t count);
    VectorIteratorWrapper &operator--();
    VectorIteratorWrapper operator--(int);
    bool operator!=(const VectorIteratorWrapper &other) const;
  };

  class MapIteratorWrapper {
  protected:
    Map::iterator iterator;
    Map::iterator end;
    ObservableCollection *collection;
    std::optional<std::pair<Key, ValueWrapper>> pair;
    friend ObservableList;
    friend ObservableDict;

  public:
    MapIteratorWrapper();
    MapIteratorWrapper(Map::iterator iterator, Map::iterator end,
                       ObservableCollection *collection);
    std::pair<Key, ValueWrapper>& operator*();
    std::pair<Key, ValueWrapper> *operator->();
    MapIteratorWrapper &operator++();
    MapIteratorWrapper operator++(int);
    MapIteratorWrapper &operator--();
    MapIteratorWrapper operator--(int);
    bool operator!=(const MapIteratorWrapper &other) const;
  };
  ObservableCollection(ObservableCollection *parent = nullptr);
  virtual ~ObservableCollection();

  static ObservableCollection::Value from_json(const boost::json::value &);
  static boost::json::value to_json(const ObservableCollection::Value &);
  static std::string to_string(const ObservableCollection::Value &);
  static std::string to_string(const ObservableCollection::Key &);
  virtual std::optional<ObservableCollection::Key>
  key_of(const ObservableCollection &v) const = 0;
  virtual void
      set_remote_storage(std::function<bool(Action, const std::string &,
                                            ObservableCollection::Value,
                                            std::function<void()>)>) = 0;
  virtual boost::json::value to_json() = 0;

  std::string address() const;
  void notify(ObservableCollection *, Action, const Key &, Value &);
};

class ObservableList : public ObservableCollection, public std::enable_shared_from_this<ObservableList>{
  Vector content;

public:
  ObservableList(ObservableCollection *parent = nullptr);
  ValueWrapper operator[](size_t i);
  const Value &operator[](size_t i) const;
  ValueWrapper at(size_t i);
  const Value &at(size_t i) const;
  VectorIteratorWrapper begin();
  Vector::const_iterator begin() const;
  VectorIteratorWrapper end();
  Vector::const_iterator end() const;
  Vector::const_iterator cend() const;
  VectorIteratorWrapper erase(VectorIteratorWrapper i);
  VectorIteratorWrapper
  erase(Vector::const_iterator i,
        std::function<void(VectorIteratorWrapper)> callback = nullptr,
        bool from_remote = false);
  VectorIteratorWrapper
  erase(size_t i, std::function<void(VectorIteratorWrapper)> callback = nullptr,
        bool from_remote = false);
  void push_back(const Value &value, std::function<void()> callback = nullptr,
                 bool from_remote = false);
  void pop_back(std::function<void()> callback = nullptr,
                bool from_remote = false);
  void clear();
  void recap();
  void recap(Observer target);
  size_t size() const;
  bool empty() const;
  ObservableList &operator=(const ObservableList &that) = delete;
  ObservableList &assign(const ObservableList &that, bool from_remote = false);
  ObservableList &operator=(const boost::json::array &that);
  ObservableList(const boost::json::array &that);
  operator boost::json::array() const;
  std::optional<ObservableCollection::Key>
  key_of(const ObservableCollection &v) const override;
  void set_remote_storage(
      std::function<bool(Action, const std::string &,
                         ObservableCollection::Value, std::function<void()>)>
          remote_storage) override;
  boost::json::value to_json() override;
};

class ObservableDict : public ObservableCollection, public std::enable_shared_from_this<ObservableDict> {
  Map content;

public:
  ObservableDict(ObservableCollection *parent = nullptr);
  ValueWrapper operator[](const Key &i);
  ValueWrapper at(const Key &i);
  const Value &at(const Key &i) const;
  bool contains(const Key &i) const;
  MapIteratorWrapper begin();
  Map::const_iterator begin() const;
  MapIteratorWrapper end();
  Map::const_iterator end() const;
  Map::const_iterator cend() const;
  MapIteratorWrapper erase(MapIteratorWrapper i);
  MapIteratorWrapper
  erase(Map::const_iterator i,
        std::function<void(MapIteratorWrapper)> callback = nullptr,
        bool from_remote = false);
  MapIteratorWrapper
  erase(const ObservableCollection::Key &i,
        std::function<void(MapIteratorWrapper)> callback = nullptr,
        bool from_remote = false);
  MapIteratorWrapper find(const Key &i);
  Map::const_iterator find(const Key &i) const;
  void clear();
  void recap();
  void recap(Observer target);
  size_t size() const;
  bool empty() const;
  ObservableDict &operator=(const ObservableDict &) = delete;
  ObservableDict &assign(const ObservableDict &that, bool from_remote = false);
  ObservableDict &operator=(const boost::json::object &that);
  ObservableDict(const boost::json::object &that);
  operator boost::json::object() const;
  std::optional<ObservableCollection::Key>
  key_of(const ObservableCollection &v) const override;
  void set_remote_storage(
      std::function<bool(Action, const std::string &,
                         ObservableCollection::Value, std::function<void()>)>
          remote_storage) override;
  boost::json::value to_json() override;
};
ObservableCollection::Value get_jsonpath(ObservableCollection::Value store,
                                         const std::list<std::string> &tokens);
ObservableCollection::Value get_jsonpath(ObservableCollection::Value store,
                                         const std::string &query);
void set_jsonpath(ObservableCollection::Value store, const std::string &query,
                  ObservableCollection::Value value, bool from_remote = false);
void delete_jsonpath(ObservableCollection::Value store,
                     const std::string &query, bool from_remote = false);
} // namespace thalamus
