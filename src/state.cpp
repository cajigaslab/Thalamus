#include <state.hpp>
#include <cstdint>

namespace thalamus {
  ObservableCollection::ValueWrapper::ValueWrapper(const Key& _key, std::function<Value& ()> _get_value, std::function<bool ()> _has_value, ObservableCollection* _collection)
      : key(_key)
      , get_value(_get_value)
      , has_value(_has_value)
      , collection(_collection) {}

  ObservableCollection::ValueWrapper::operator ObservableDictPtr() {
    auto value = get_value();
    if (std::holds_alternative<ObservableDictPtr>(value)) {
      return thalamus::get<ObservableDictPtr>(value);
    } else {
      THALAMUS_ASSERT(false);
    }
  }
 
  ObservableCollection::ValueWrapper::operator ObservableListPtr() {
    auto value = get_value();
    if (std::holds_alternative<ObservableListPtr>(value)) {
      return thalamus::get<ObservableListPtr>(value);
    } else {
      THALAMUS_ASSERT(false);
    }
  }

  ObservableCollection::ValueWrapper::operator long long int() {
    auto value = get_value();
    if (std::holds_alternative<long long int>(value)) {
      return thalamus::get<long long int>(value);
    }
    else if (std::holds_alternative<double>(value)) {
      return int64_t(thalamus::get<double>(value));
    }
    else {
      THALAMUS_ASSERT(false);
    }
  }
  ObservableCollection::ValueWrapper::operator unsigned long long int() {
    auto value = get_value();
    if (std::holds_alternative<long long int>(value)) {
      return uint64_t(thalamus::get<long long int>(value));
    }
    else if (std::holds_alternative<double>(value)) {
      return uint64_t(thalamus::get<double>(value));
    }
    else {
      THALAMUS_ASSERT(false);
    }
  }
  ObservableCollection::ValueWrapper::operator unsigned long() {
    auto value = get_value();
    if (std::holds_alternative<long long int>(value)) {
      return uint32_t(thalamus::get<long long int>(value));
    }
    else if (std::holds_alternative<double>(value)) {
      return uint32_t(thalamus::get<double>(value));
    }
    else {
      THALAMUS_ASSERT(false);
    }
  }
  ObservableCollection::ValueWrapper::operator double() {
    auto value = get_value();
    if (std::holds_alternative<long long int>(value)) {
      return double(thalamus::get<long long int>(value));
    }
    else if (std::holds_alternative<double>(value)) {
      return thalamus::get<double>(value);
    }
    else {
      THALAMUS_ASSERT(false);
    }
  }
  ObservableCollection::ValueWrapper::operator bool() {
    auto value = get_value();
    if (std::holds_alternative<long long int>(value)) {
      return thalamus::get<long long int>(value) != 0;
    } else if (std::holds_alternative<double>(value)) {
#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wfloat-equal"
#endif
      return thalamus::get<double>(value) != 0;
#ifdef __clang__
  #pragma clang diagnostic pop
#endif
    } else if (std::holds_alternative<bool>(value)) {
      return thalamus::get<bool>(value);
    }
    else {
      THALAMUS_ASSERT(false);
    }
  }
  ObservableCollection::ValueWrapper::operator std::string() {
    auto value = get_value();
    if (std::holds_alternative<std::string>(value)) {
      return thalamus::get<std::string>(value);
    }
    else {
      THALAMUS_ASSERT(false);
    }
  }
  ObservableCollection::ValueWrapper::operator ObservableCollection::Value() {
    return get_value();
  }
  ObservableCollection::Value ObservableCollection::ValueWrapper::get() {
    return get_value();
  }
  bool ObservableCollection::ValueWrapper::operator==(const Value& other) {
    return get_value() == other;
  }


  ObservableCollection::VectorIteratorWrapper::VectorIteratorWrapper()
    : key(0)
    , iterator()
    , end()
    , collection(nullptr)
  {}
  ObservableCollection::VectorIteratorWrapper::VectorIteratorWrapper(size_t _key, Vector::iterator _iterator, Vector::iterator _end, ObservableCollection* _collection)
    : key(_key)
    , iterator(_iterator)
    , end(_end)
    , collection(_collection) {}

  ObservableCollection::ValueWrapper ObservableCollection::VectorIteratorWrapper::operator*() {
    auto _iterator = this->iterator;
    auto _end = this->end;
    return ValueWrapper(static_cast<long long int>(key), [_iterator]() -> Value& { return *_iterator; }, [_iterator,_end]() -> bool { return _iterator != _end; }, collection);
  }

  ObservableCollection::VectorIteratorWrapper& ObservableCollection::VectorIteratorWrapper::operator+(size_t count) {
    key += count;
    iterator += int64_t(count);
    return *this;
  }

  ObservableCollection::VectorIteratorWrapper& ObservableCollection::VectorIteratorWrapper::operator+=(size_t count) {
    return *this + count;
  }

  ObservableCollection::VectorIteratorWrapper& ObservableCollection::VectorIteratorWrapper::operator++() {
    return *this += 1;
  }

  ObservableCollection::VectorIteratorWrapper ObservableCollection::VectorIteratorWrapper::operator++(int) {
    auto new_wrapper = *this;
    ++* this;
    return new_wrapper;
  }

  ObservableCollection::VectorIteratorWrapper& ObservableCollection::VectorIteratorWrapper::operator-(size_t count) {
    return *this + -count;
  }

  ObservableCollection::VectorIteratorWrapper& ObservableCollection::VectorIteratorWrapper::operator-=(size_t count) {
    return *this - count;
  }

  ObservableCollection::VectorIteratorWrapper& ObservableCollection::VectorIteratorWrapper::operator--() {
    return *this -= 1;
  }

  ObservableCollection::VectorIteratorWrapper ObservableCollection::VectorIteratorWrapper::operator--(int) {
    auto new_wrapper = *this;
    --* this;
    return new_wrapper;
  }

  bool ObservableCollection::VectorIteratorWrapper::operator!=(const VectorIteratorWrapper& other) const {
    return iterator != other.iterator;
  }


  ObservableCollection::MapIteratorWrapper::MapIteratorWrapper()
    : iterator()
    , end()
    , collection(nullptr) {}

  ObservableCollection::MapIteratorWrapper::MapIteratorWrapper(Map::iterator _iterator, Map::iterator _end, ObservableCollection* _collection)
    : iterator(_iterator)
    , end(_end)
    , collection(_collection) {}

  ObservableCollection::ValueWrapper ObservableCollection::MapIteratorWrapper::operator*() {
    auto _iterator = this->iterator;
    auto _end = this->end;
    return ValueWrapper(_iterator->first, [_iterator]() -> Value& { return _iterator->second; }, [_iterator,_end]() -> bool { return _iterator != _end; }, collection);
  }

  std::pair<ObservableCollection::Key, ObservableCollection::ValueWrapper>* ObservableCollection::MapIteratorWrapper::operator->() {
    auto _iterator = this->iterator;
    auto _end = this->end;
    pair = std::make_pair(_iterator->first, ValueWrapper(_iterator->first, [_iterator]() -> Value& { return _iterator->second; }, [_iterator,_end]() -> bool { return _iterator != _end; }, collection));
    return &pair.value();
  }

  ObservableCollection::MapIteratorWrapper& ObservableCollection::MapIteratorWrapper::operator++() {
    ++iterator;
    return *this;
  }

  ObservableCollection::MapIteratorWrapper ObservableCollection::MapIteratorWrapper::operator++(int) {
    auto new_wrapper = *this;
    ++* this;
    return new_wrapper;
  }

  ObservableCollection::MapIteratorWrapper& ObservableCollection::MapIteratorWrapper::operator--() {
    --iterator;
    return *this;
  }

  ObservableCollection::MapIteratorWrapper ObservableCollection::MapIteratorWrapper::operator--(int) {
    auto new_wrapper = *this;
    --* this;
    return new_wrapper;
  }

  bool ObservableCollection::MapIteratorWrapper::operator!=(const MapIteratorWrapper& other) const {
    return iterator != other.iterator;
  }


  ObservableCollection::ObservableCollection(ObservableCollection* _parent)
    : parent(_parent) {}

  std::string ObservableCollection::address() const {
    if (!parent) {
      return "";
    }
    auto prefix = parent->address();
    auto end_opt = parent->key_of(*this);
    THALAMUS_ASSERT(end_opt.has_value(), "Failed to find self in parent collection");
    auto end = *end_opt;
    if (std::holds_alternative<long long int>(end)) {
      return absl::StrFormat("%s[%d]", prefix, thalamus::get<long long int>(end));
    }
    else if (std::holds_alternative<std::string>(end)) {
      if (prefix.empty()) {
        return absl::StrFormat("['%s']", thalamus::get<std::string>(end));
      }
      else {
        return absl::StrFormat("%s['%s']", prefix, thalamus::get<std::string>(end));
      }
    }
    else {
      THALAMUS_ASSERT(false, "Unsupported key type");
      return "";
    }
  }

  void ObservableCollection::notify(ObservableCollection* source, Action action, const Key& key, Value& value) {
    if(source == this) {
      changed(action, key, value);
    }

    recursive_changed(source, action, key, value);

    if(parent) {
      parent->notify(source, action, key, value);
    }
  }

  ObservableList::ObservableList(ObservableCollection* _parent)
    : ObservableCollection(_parent)
    , content(Vector()) {
  }

  ObservableList::ValueWrapper ObservableList::operator[](size_t i) {
    return ValueWrapper(static_cast<long long int>(i), [this, i]() -> Value& { return content[i]; }, [this, i]() -> bool { return i < content.size(); }, this);
  }

  const ObservableList::Value& ObservableList::operator[](size_t i) const {
    return content[i];
  }

  ObservableList::ValueWrapper ObservableList::at(size_t i) {
    return ValueWrapper(static_cast<long long int>(i), [this, i]() -> Value& { return content.at(i); }, [this, i]() -> bool { return i < content.size(); }, this);
  }

  const ObservableList::Value& ObservableList::at(size_t i) const {
    return content.at(i);
  }

  ObservableList::VectorIteratorWrapper ObservableList::begin() {
    return VectorIteratorWrapper(0, content.begin(), content.end(), this);
  }

  ObservableList::Vector::const_iterator ObservableList::begin() const {
    return content.begin();
  }

  ObservableList::VectorIteratorWrapper ObservableList::end() {
    return VectorIteratorWrapper(content.size(), content.end(), content.end(), this);
  }

  ObservableList::Vector::const_iterator ObservableList::end() const {
    return content.end();
  }

  ObservableList::Vector::const_iterator ObservableList::cend() const {
    return content.end();
  }

  ObservableList::VectorIteratorWrapper ObservableList::erase(VectorIteratorWrapper i) {
    return erase(i.iterator);
  }

  ObservableList::VectorIteratorWrapper ObservableList::erase(Vector::const_iterator i, std::function<void(VectorIteratorWrapper)> callback, bool from_remote) {
    auto key = std::distance(content.cbegin(), i);
    if (!callback) {
      callback = [](VectorIteratorWrapper) {};
    }

    if (!from_remote && this->remote_storage) {
      auto callback_wrapper = [this, key, callback] {
        auto key_after = std::min(key, ptrdiff_t(content.size()));
        callback(VectorIteratorWrapper(size_t(key_after), content.begin() + key_after, content.end(), this));
      };
      if (this->remote_storage(Action::Delete, address() + "[" + std::to_string(key) + "]", *i, callback_wrapper)) {
        return VectorIteratorWrapper();
      }
    }

    if (std::holds_alternative<ObservableListPtr>(*i)) {
      auto temp = thalamus::get<ObservableListPtr>(*i);
      temp->set_remote_storage(decltype(this->remote_storage)());
    }
    else if (std::holds_alternative<ObservableDictPtr>(*i)) {
      auto temp = thalamus::get<ObservableDictPtr>(*i);
      temp->set_remote_storage(decltype(this->remote_storage)());
    }

    auto value = *i;
    auto i2 = content.erase(i);
    notify(this, Action::Delete, key, value);

    auto distance = std::distance(content.begin(), i2);
    return VectorIteratorWrapper(size_t(distance), i2, content.end(), this);
  }

  ObservableList::VectorIteratorWrapper ObservableList::erase(size_t i, std::function<void(VectorIteratorWrapper)> callback, bool from_remote) {
    return erase(this->content.begin() + int64_t(i), callback, from_remote);
  }

  void ObservableList::clear() {
    while (!content.empty()) {
      pop_back();
    }
  }

  void ObservableList::recap() {
    long long int i = 0;
    for (auto& v : content) {
      notify(this, Action::Set, i++, v);
    }
  }

  void ObservableList::recap(Observer target) {
    long long int i = 0;
    for (auto& v : content) {
      target(Action::Set, i++, v);
    }
  }

  size_t ObservableList::size() const {
    return content.size();
  }

  bool ObservableList::empty() const {
    return content.empty();
  }

  ObservableList& ObservableList::operator=(const boost::json::array& that) {
    ObservableList temp(that);
    return this->assign(temp);
  }

  std::optional<ObservableCollection::Key> ObservableList::key_of(const ObservableCollection& v) const {
    auto i = 0;
    for (const auto& our_value : content) {
      if (std::holds_alternative<ObservableListPtr>(our_value)) {
        auto temp = thalamus::get<ObservableListPtr>(our_value);
        auto temp2 = std::static_pointer_cast<ObservableCollection>(temp);
        if (temp2.get() == &v) {
          return i;
        }
      }
      else if (std::holds_alternative<ObservableDictPtr>(our_value)) {
        auto temp = thalamus::get<ObservableDictPtr>(our_value);
        auto temp2 = std::static_pointer_cast<ObservableCollection>(temp);
        if (temp2.get() == &v) {
          return i;
        }
      }
      ++i;
    }
    return std::nullopt;
  }

  void ObservableList::push_back(const Value& value, std::function<void()> callback, bool from_remote) {
    if (!callback) {
      callback = [] {};
    }

    if (!from_remote && remote_storage) {
      if (this->remote_storage(Action::Set, address() + "[" + std::to_string(content.size()) + "]", value, callback)) {
        return;
      }
    }

    if (std::holds_alternative<ObservableDictPtr>(value)) {
      thalamus::get<ObservableDictPtr>(value)->parent = this;
      thalamus::get<ObservableDictPtr>(value)->set_remote_storage(remote_storage);
    }
    else if (std::holds_alternative<ObservableListPtr>(value)) {
      thalamus::get<ObservableListPtr>(value)->parent = this;
      thalamus::get<ObservableListPtr>(value)->set_remote_storage(remote_storage);
    }
    content.push_back(value);
    notify(this, Action::Set, static_cast<long long>(content.size() - 1), content.back());
  }

  void ObservableList::pop_back(std::function<void()> callback, bool from_remote) {
    if (!callback) {
      callback = [] {};
    }

    auto value = content.back();
    if (!from_remote && remote_storage) {
      if (this->remote_storage(Action::Delete, address() + "[" + std::to_string(content.size() - 1) + "]", value, callback)) {
        return;
      }
    }

    if (std::holds_alternative<ObservableDictPtr>(value)) {
      thalamus::get<ObservableDictPtr>(value)->parent = nullptr;
    }
    else if (std::holds_alternative<ObservableListPtr>(value)) {
      thalamus::get<ObservableListPtr>(value)->parent = nullptr;
    }
    content.pop_back();
    notify(this, Action::Delete, static_cast<long long>(content.size()), value);
  }

  void ObservableCollection::ValueWrapper::assign(const Value& new_value, std::function<void()> callback, bool from_remote) {
    if (!callback) {
      callback = [] {};
    }

    if(has_value()) {
      auto& value = get_value();
      if(value == new_value) {
        callback();
        return;
      }
    }

    if (!from_remote && collection->remote_storage) {
      auto address = collection->address();
      if (std::holds_alternative<std::string>(key)) {
        address += "['" + to_string(key) + "']";
      }
      else {
        address += "[" + to_string(key) + "]";
      }

      if (collection->remote_storage(Action::Set, address, new_value, callback)) {
        return;
      }
    }

    auto& value = get_value();
    value = new_value;
    if (std::holds_alternative<ObservableDictPtr>(value)) {
      thalamus::get<ObservableDictPtr>(value)->parent = collection;
      thalamus::get<ObservableDictPtr>(value)->set_remote_storage(collection->remote_storage);
    }
    else if (std::holds_alternative<ObservableListPtr>(value)) {
        thalamus::get<ObservableListPtr>(value)->parent = collection;
      thalamus::get<ObservableListPtr>(value)->set_remote_storage(collection->remote_storage);
    }
    collection->notify(collection, Action::Set, key, value);
    return;
  }

  ObservableList& ObservableList::assign(const ObservableList& that, bool from_remote) {
    for (auto i = 0ull; i < that.content.size(); ++i) {
      auto& source = that.content[i];
      if (i >= this->size()) {
        this->content.emplace_back();
      }
      auto target = (*this)[i];
      ObservableCollection::Value target_value = target;
      if (target_value.index() == source.index()) {
        if (std::holds_alternative<ObservableDictPtr>(target_value)) {
          ObservableDictPtr target_dict = target;
          target_dict->assign(*thalamus::get<ObservableDictPtr>(source));
          continue;
        }
        else if (std::holds_alternative<ObservableListPtr>(target_value)) {
          ObservableListPtr target_list = target;
          target_list->assign(*thalamus::get<ObservableListPtr>(source));
          continue;
        }
      }

      (*this)[i].assign(that.at(i), [] {}, from_remote);
    }
    for (auto i = that.content.size(); i < content.size(); ++i) {
      this->erase(i, nullptr, from_remote);
    }
    return *this;
  }

  ObservableList::ObservableList(const boost::json::array& that) {
    ObservableDictPtr dict;
    ObservableListPtr list;
    for (auto& v : that) {
      switch (v.kind()) {
      case boost::json::kind::object: {
        dict = std::make_shared<ObservableDict>(v.as_object());
        dict->parent = this;
        dict->set_remote_storage(remote_storage);
        content.push_back(dict);
        break;
      }
      case boost::json::kind::array: {
        list = std::make_shared<ObservableList>(v.as_array());
        list->parent = this;
        list->set_remote_storage(remote_storage);
        content.push_back(list);
        break;
      }
      case boost::json::kind::string: {
        content.push_back(std::string(v.as_string()));
        break;
      }
      case boost::json::kind::uint64: {
        content.push_back(static_cast<long long>(v.as_uint64()));
        break;
      }
      case boost::json::kind::int64: {
        content.push_back(v.as_int64());
        break;
      }
      case boost::json::kind::double_: {
        content.push_back(v.as_double());
        break;
      }
      case boost::json::kind::bool_: {
        content.push_back(v.as_bool());
        break;
      }
      case boost::json::kind::null: {
        content.push_back(std::monostate());
        break;
      }
      }
    }
  }

  ObservableList::operator boost::json::array() const {
    boost::json::array result;
    for (const auto& value : content) {
      //std::shared_ptr<ObservableDict>, std::shared_ptr<ObservableList>
      if (std::holds_alternative<long long int>(value)) {
        result.emplace_back(thalamus::get<long long int>(value));
      }
      else if (std::holds_alternative<double>(value)) {
        result.emplace_back(thalamus::get<double>(value));
      }
      else if (std::holds_alternative<bool>(value)) {
        result.emplace_back(thalamus::get<bool>(value));
      }
      else if (std::holds_alternative<std::string>(value)) {
        result.emplace_back(thalamus::get<std::string>(value));
      }
      else if (std::holds_alternative<std::shared_ptr<ObservableDict>>(value)) {
        result.emplace_back(boost::json::object(*thalamus::get<std::shared_ptr<ObservableDict>>(value)));
      }
      else if (std::holds_alternative<std::shared_ptr<ObservableList>>(value)) {
        result.emplace_back(boost::json::array(*thalamus::get<std::shared_ptr<ObservableList>>(value)));
      }
    }
    return result;
  }

  ObservableCollection::Value ObservableCollection::from_json(const boost::json::value& value) {
    switch (value.kind()) {
    case boost::json::kind::object: {
      return std::make_shared<ObservableDict>(value.as_object());
    }
    case boost::json::kind::array: {
      return std::make_shared<ObservableList>(value.as_array());
    }
    case boost::json::kind::string: {
      return std::string(value.as_string());
    }
    case boost::json::kind::uint64: {
      return static_cast<long long>(value.as_uint64());
    }
    case boost::json::kind::int64: {
      return value.as_int64();
    }
    case boost::json::kind::double_: {
      return value.as_double();
    }
    case boost::json::kind::bool_: {
      return value.as_bool();
    }
    case boost::json::kind::null: {
      return std::monostate();
    }
    }
  }
  boost::json::value ObservableCollection::to_json(const ObservableCollection::Value& value) {
    if (std::holds_alternative<long long int>(value)) {
      return thalamus::get<long long int>(value);
    }
    else if (std::holds_alternative<double>(value)) {
      return thalamus::get<double>(value);
    }
    else if (std::holds_alternative<bool>(value)) {
      return thalamus::get<bool>(value);
    }
    else if (std::holds_alternative<std::string>(value)) {
      return boost::json::string(thalamus::get<std::string>(value));
    }
    else if (std::holds_alternative<std::shared_ptr<ObservableDict>>(value)) {
      boost::json::object result = *thalamus::get<std::shared_ptr<ObservableDict>>(value);
      return result;
    }
    else if (std::holds_alternative<std::shared_ptr<ObservableList>>(value)) {
      boost::json::array result = *thalamus::get<std::shared_ptr<ObservableList>>(value);
      return result;
    }
    return boost::json::value();
  }
  std::string ObservableCollection::to_string(const ObservableCollection::Value& value) {
    if (std::holds_alternative<long long int>(value)) {
      return std::to_string(thalamus::get<long long int>(value));
    }
    else if (std::holds_alternative<double>(value)) {
      return std::to_string(thalamus::get<double>(value));
    }
    else if (std::holds_alternative<bool>(value)) {
      return std::to_string(thalamus::get<bool>(value));
    }
    else if (std::holds_alternative<std::string>(value)) {
      return thalamus::get<std::string>(value);
    }
    else if (std::holds_alternative<std::shared_ptr<ObservableDict>>(value)) {
      boost::json::object result = *thalamus::get<std::shared_ptr<ObservableDict>>(value);
      return boost::json::serialize(result);
    }
    else if (std::holds_alternative<std::shared_ptr<ObservableList>>(value)) {
      boost::json::array result = *thalamus::get<std::shared_ptr<ObservableList>>(value);
      return boost::json::serialize(result);
    }
    BOOST_ASSERT_MSG(false, "Failed to convert value to string");
    return "";
  }
  std::string ObservableCollection::to_string(const ObservableCollection::Key& value) {
    if (std::holds_alternative<long long int>(value)) {
      return std::to_string(thalamus::get<long long int>(value));
    }
    else if (std::holds_alternative<bool>(value)) {
      return std::to_string(thalamus::get<bool>(value));
    }
    else if (std::holds_alternative<std::string>(value)) {
      return thalamus::get<std::string>(value);
    }
    BOOST_ASSERT_MSG(false, "Failed to convert key to string");
    return "";
  }

  ObservableCollection::Value get_jsonpath(ObservableCollection::Value store, const std::list<std::string>& tokens) {
    ObservableCollection::Value current = store;
    for (auto& token : tokens) {
      if (std::holds_alternative<ObservableDictPtr>(current)) {
        const auto& held = thalamus::get< ObservableDictPtr>(current);
        current = held->at(token);
      }
      else if (std::holds_alternative<ObservableListPtr>(current)) {
        const auto& held = thalamus::get< ObservableListPtr>(current);
        size_t index;
        auto success = absl::SimpleAtoi(token, &index);
        BOOST_ASSERT_MSG(success, "Failed to convert index into number");
        current = held->at(index);
      }
      else {
        BOOST_ASSERT_MSG(false, "Attempted to index something that isn't a collection");
      }
    }
    return current;
  }

  ObservableCollection::Value get_jsonpath(ObservableCollection::Value store, const std::string& query) {
    std::list<std::string> tokens = absl::StrSplit(query, absl::ByAnyChar("[].'\""), absl::SkipEmpty());
    return get_jsonpath(store, tokens);
  }

  void set_jsonpath(ObservableCollection::Value store, const std::string& query, ObservableCollection::Value value, bool from_remote) {
    std::list<std::string> tokens = absl::StrSplit(query, absl::ByAnyChar("[].'\""), absl::SkipEmpty());
    ObservableCollection::Value current = store;
    std::string end;
    if (!tokens.empty()) {
      end = tokens.back();
      tokens.pop_back();
      current = get_jsonpath(store, tokens);
    }

    if (std::holds_alternative<ObservableDictPtr>(current)) {
      auto held = thalamus::get<ObservableDictPtr>(current);
      if (end.empty()) {
        BOOST_ASSERT(std::holds_alternative<ObservableDictPtr>(value));
        auto unwrapped_value = thalamus::get< ObservableDictPtr>(value);
        held->assign(*unwrapped_value, from_remote);
      }
      else {
        (*held)[end].assign(value, nullptr, from_remote);
      }
    }
    else if (std::holds_alternative<ObservableListPtr>(current)) {
      auto held = thalamus::get<ObservableListPtr>(current);
      if (end.empty()) {
        BOOST_ASSERT(std::holds_alternative<ObservableListPtr>(value));
        auto unwrapped_value = thalamus::get< ObservableListPtr>(value);
        held->assign(*unwrapped_value, from_remote);
      }
      else {
        size_t index;
        auto success = absl::SimpleAtoi(end, &index);
        BOOST_ASSERT_MSG(success, "Failed to convert index into number");
        while (held->size() < index) {
          held->push_back(ObservableCollection::Value(), nullptr, from_remote);
        }
        BOOST_ASSERT_MSG(index <= held->size(), "index must be less than or equal to array size");
        if (held->size() == index) {
          held->push_back(value, nullptr, from_remote);
        }
        else {
          held->at(index).assign(value, nullptr, from_remote);
        }
      }
    }
    else {
      BOOST_ASSERT_MSG(false, "Attempted to index something that isn't a collection");
    }
  }

  void delete_jsonpath(ObservableCollection::Value store, const std::string& query, bool from_remote) {
    std::list<std::string> tokens = absl::StrSplit(query, absl::ByAnyChar("[].'\""), absl::SkipEmpty());
    BOOST_ASSERT_MSG(!tokens.empty(), "Cant delete root");
    ObservableCollection::Value current = store;
    std::string end = tokens.back();
    tokens.pop_back();
    current = get_jsonpath(store, tokens);

    if (std::holds_alternative<ObservableDictPtr>(current)) {
      auto held = thalamus::get<ObservableDictPtr>(current);
      held->erase(end, [](auto) {}, from_remote);
    }
    else if (std::holds_alternative<ObservableListPtr>(current)) {
      auto held = thalamus::get<ObservableListPtr>(current);
      size_t index;
      auto success = absl::SimpleAtoi(end, &index);
      BOOST_ASSERT_MSG(success, "Failed to convert index into number");
      held->erase(index, [] (auto) {}, from_remote);
    }
    else {
      BOOST_ASSERT_MSG(false, "Attempted to index something that isn't a collection");
    }
  }

  void ObservableList::set_remote_storage(std::function<bool(Action, const std::string&, ObservableCollection::Value, std::function<void()>)> _remote_storage) {
    this->remote_storage = _remote_storage;
    for (auto& c : content) {
      if (std::holds_alternative<ObservableListPtr>(c)) {
        auto temp = thalamus::get<ObservableListPtr>(c);
        temp->set_remote_storage(_remote_storage);
      }
      else if (std::holds_alternative<ObservableDictPtr>(c)) {
        auto temp = thalamus::get<ObservableDictPtr>(c);
        temp->set_remote_storage(_remote_storage);
      }
    }
  }


  ObservableDict::ObservableDict(ObservableCollection* _parent)
    : ObservableCollection(_parent)
    , content(Map()) {
  }

  ObservableDict::ValueWrapper ObservableDict::operator[](const Key& i) {
    return ValueWrapper(i, [this, i]() -> Value& { return content[i]; }, [this, i]() -> bool { return content.contains(i); }, this);
  }

  ObservableDict::ValueWrapper ObservableDict::at(const Key& i) {
    return ValueWrapper(i, [this, i]() -> Value& { return content.at(i); }, [this, i]() -> bool { return content.contains(i); }, this);
  }

  const ObservableDict::Value& ObservableDict::at(const ObservableDict::Key& i) const {
    return content.at(i);
  }

  bool ObservableDict::contains(const ObservableDict::Key& i) const {
    return content.contains(i);
  }

  ObservableDict::MapIteratorWrapper ObservableDict::begin() {
    return MapIteratorWrapper(content.begin(), content.end(), this);
  }

  ObservableDict::Map::const_iterator ObservableDict::begin() const {
    return content.begin();
  }

  ObservableDict::MapIteratorWrapper ObservableDict::end() {
    return MapIteratorWrapper(content.end(), content.end(), this);
  }

  ObservableDict::Map::const_iterator ObservableDict::end() const {
    return content.end();
  }

  ObservableDict::Map::const_iterator ObservableDict::cend() const {
    return content.end();
  }

  ObservableDict::MapIteratorWrapper ObservableDict::erase(MapIteratorWrapper i) {
    return erase(i.iterator);
  }

  ObservableDict::MapIteratorWrapper ObservableDict::erase(Map::const_iterator i, std::function<void(MapIteratorWrapper)> callback, bool from_remote) {
    auto pair = *i;
    auto next_i = i;
    ++next_i;
    if (!callback) {
      callback = [](MapIteratorWrapper) {};
    }

    if (!from_remote && this->remote_storage) {
      std::function<void()> callback_wrapper;
      if (next_i == content.end()) {
        callback_wrapper = [callback] { callback(MapIteratorWrapper()); };
      } else {
        callback_wrapper = [this, next_pair = *next_i, callback] {
          callback(this->find(next_pair.first));
        };
      }
      auto address = this->address() + "['" + to_string(pair.first) + "']";
      if (this->remote_storage(Action::Delete, address, pair.second, callback_wrapper)) {
        return MapIteratorWrapper();
      }
    }

    if (std::holds_alternative<ObservableListPtr>(i->second)) {
      auto temp = thalamus::get<ObservableListPtr>(i->second);
      temp->set_remote_storage(decltype(this->remote_storage)());
    }
    else if (std::holds_alternative<ObservableDictPtr>(i->second)) {
      auto temp = thalamus::get<ObservableDictPtr>(i->second);
      temp->set_remote_storage(decltype(this->remote_storage)());
    }

    auto i2 = content.erase(i);
    notify(this, Action::Delete, pair.first, pair.second);

    return MapIteratorWrapper(i2, content.end(), this);
  }

  ObservableDict::MapIteratorWrapper ObservableDict::erase(const ObservableCollection::Key& i, std::function<void(MapIteratorWrapper)> callback, bool from_remote) {
    return erase(content.find(i), callback, from_remote);
  }

  ObservableDict::MapIteratorWrapper ObservableDict::find(const Key& i) {
    return MapIteratorWrapper(content.find(i), content.end(), this);
  }

  ObservableDict::Map::const_iterator ObservableDict::find(const Key& i) const {
    return content.find(i);
  }

  void ObservableDict::clear() {
    while (!content.empty()) {
      auto the_end = end();
      --the_end;
      erase(the_end);
    }
  }

  void ObservableDict::recap() {
    for (auto& i : content) {
      notify(this, Action::Set, i.first, i.second);
    }
  }

  void ObservableDict::recap(Observer target) {
    for (auto& i : content) {
      target(Action::Set, i.first, i.second);
    }
  }

  size_t ObservableDict::size() const {
    return content.size();
  }

  bool ObservableDict::empty() const {
    return content.empty();
  }

  ObservableDict& ObservableDict::assign(const ObservableDict& that, bool from_remote) {
    std::set<ObservableCollection::Key> missing;
    for (auto& i : content) {
      missing.insert(i.first);
    }
    for (auto& i : that.content) {
      missing.erase(i.first);
      auto target = (*this)[i.first];
      ObservableCollection::Value target_value = target;
      if (target_value.index() == i.second.index()) {
        if (std::holds_alternative<ObservableDictPtr>(target_value)) {
          ObservableDictPtr target_dict = target;
          target_dict->assign(*thalamus::get<ObservableDictPtr>(i.second), from_remote);
          continue;
        }
        else if (std::holds_alternative<ObservableListPtr>(target_value)) {
          ObservableListPtr target_list = target;
          target_list->assign(*thalamus::get<ObservableListPtr>(i.second), from_remote);
          continue;
        }
      }
      target.assign(i.second, [] {}, from_remote);
    }
    for (auto& i : missing) {
      this->erase(i, nullptr, from_remote);
    }
    return *this;
  }

  ObservableDict& ObservableDict::operator=(const boost::json::object& that) {
    ObservableDict temp(that);
    return this->assign(temp);
  }

  ObservableDict::ObservableDict(const boost::json::object& that) {
    ObservableDictPtr dict;
    ObservableListPtr list;
    for (auto& v : that) {
      switch (v.value().kind()) {
      case boost::json::kind::object: {
        dict = std::make_shared<ObservableDict>(v.value().as_object());
        dict->parent = this;
        dict->set_remote_storage(remote_storage);
        content[v.key()] = dict;
        break;
      }
      case boost::json::kind::array: {
        list = std::make_shared<ObservableList>(v.value().as_array());
        list->parent = this;
        list->set_remote_storage(remote_storage);
        content[v.key()] = list;
        break;
      }
      case boost::json::kind::string: {
        content[v.key()] = std::string(v.value().as_string());
        break;
      }
      case boost::json::kind::uint64: {
        content[v.key()] = static_cast<long long>(v.value().as_uint64());
        break;
      }
      case boost::json::kind::int64: {
        content[v.key()] = v.value().as_int64();
        break;
      }
      case boost::json::kind::double_: {
        content[v.key()] = v.value().as_double();
        break;
      }
      case boost::json::kind::bool_: {
        content[v.key()] = v.value().as_bool();
        break;
      }
      case boost::json::kind::null: {
        content[v.key()] = std::monostate();
        break;
      }
      }
    }
  }

  ObservableDict::operator boost::json::object() const {
    boost::json::object result;
    for (const auto& pair : content) {
      //std::shared_ptr<ObservableDict>, std::shared_ptr<ObservableList>

      std::string key;
      if (std::holds_alternative<long long int>(pair.first)) {
        key = std::to_string(thalamus::get<long long int>(pair.first));
      } else if (std::holds_alternative<std::string>(pair.first)) {
        key = thalamus::get<std::string>(pair.first);
      } else {
        THALAMUS_ABORT("Unexpect key type");
      }

      if (std::holds_alternative<long long int>(pair.second)) {
        result[key] = thalamus::get<long long int>(pair.second);
      }
      else if (std::holds_alternative<double>(pair.second)) {
        result[key] = thalamus::get<double>(pair.second);
      }
      else if (std::holds_alternative<bool>(pair.second)) {
        result[key] = thalamus::get<bool>(pair.second);
      }
      else if (std::holds_alternative<std::string>(pair.second)) {
        result[key] = thalamus::get<std::string>(pair.second);
      }
      else if (std::holds_alternative<std::shared_ptr<ObservableDict>>(pair.second)) {
        result[key] = boost::json::object(*thalamus::get<std::shared_ptr<ObservableDict>>(pair.second));
      }
      else if (std::holds_alternative<std::shared_ptr<ObservableList>>(pair.second)) {
        result[key] = boost::json::array(*thalamus::get<std::shared_ptr<ObservableList>>(pair.second));
      }
    }
    return result;
  }

  std::optional<ObservableCollection::Key> ObservableDict::key_of(const ObservableCollection& v) const {
    for (const auto& our_pair : content) {
      const auto& our_value = our_pair.second;
      if (std::holds_alternative<ObservableListPtr>(our_value)) {
        auto temp = thalamus::get<ObservableListPtr>(our_value);
        auto temp2 = std::static_pointer_cast<ObservableCollection>(temp);
        if (temp2.get() == &v) {
          return our_pair.first;
        }
      }
      else if (std::holds_alternative<ObservableDictPtr>(our_value)) {
        auto temp = thalamus::get<ObservableDictPtr>(our_value);
        auto temp2 = std::static_pointer_cast<ObservableCollection>(temp);
        if (temp2.get() == &v) {
          return our_pair.first;
        }
      }
    }
    return std::nullopt;
  }

  void ObservableDict::set_remote_storage(std::function<bool(Action, const std::string&, ObservableCollection::Value, std::function<void()>)> _remote_storage) {
    this->remote_storage = _remote_storage;
    for (auto& c : content) {
      if (std::holds_alternative<ObservableListPtr>(c.second)) {
        auto temp = thalamus::get<ObservableListPtr>(c.second);
        temp->set_remote_storage(_remote_storage);
      }
      else if (std::holds_alternative<ObservableDictPtr>(c.second)) {
        auto temp = thalamus::get<ObservableDictPtr>(c.second);
        temp->set_remote_storage(_remote_storage);
      }
    }
  }
}
