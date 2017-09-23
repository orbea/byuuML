#ifndef LIBBYUUMLHH
#define LIBBYUUMLHH

#include <memory>
#include <string>

/*
  tl;dr: implement byuuML::reader, pass one to the constructor of
  byuuML::document, use iterators to access the document tree.
 */

namespace byuuML {
  /*
    `class T : auto_list<T>` defines a class, `T`, that is always a member of
    a linked list. Each element contains a pointer to the next element in the
    list, and also owns that element.
   */
  template<class T> class auto_list {
    auto_list(const auto_list& other) = delete;
    auto_list(auto_list&& other) = delete;
    auto_list& operator=(const auto_list& other) = delete;
    auto_list& operator=(auto_list&& other) = delete;
  protected:
    std::unique_ptr<T> next;
  public:
    auto_list() {}
    auto_list(std::unique_ptr<T> next) : next(std::move(next)) {}
    const T* get_next() const { return next.get(); }
    T* get_next() { return next.get(); }
  };
  class node : protected auto_list<node> {
    const std::unique_ptr<node> children;
  public:
    const std::string name;
    const std::string data;
    class const_iterator {
      const node* p;
    public:
      const_iterator(const node* p) : p(p) {}
      const_iterator& operator++() { p = p->get_next(); return *this; }
      bool operator==(const const_iterator& other) const { return p == other.p; }
      bool operator!=(const const_iterator& other) const { return p != other.p; }
      const node& operator*() const { return *p; }
      const node& operator->() const { return *p; }
    };
    node(std::string name, std::string data, std::unique_ptr<node> children,
         std::unique_ptr<node> next_node)
      : auto_list(std::move(next_node)),
        children(std::move(children)), name(name), data(data) {}
    const_iterator begin() const { return const_iterator(children.get()); }
    const_iterator cbegin() const { return begin(); }
    const_iterator end() const { return const_iterator(nullptr); }
    const_iterator cend() const { return end(); }
    bool has_children() const { return children.get() != nullptr; }
  };
  class reader {
  public:
    // Signal EOF by making begin == end
    virtual void read_more(const char*& begin, const char*& end) = 0;
  };
  class document {
    std::unique_ptr<node> nodes;
    document(const document& other) = delete;
    document& operator=(const document& other) = delete;
  public:
    document(document&& other) {
      nodes.swap(other.nodes);
    }
    document& operator=(document&& other) {
      nodes.reset();
      nodes.swap(other.nodes);
      return *this;
    }
    // will throw a std::string on failure
    // max_depth is approximate, it may sometimes be exceeded by one
    document(reader&, size_t max_depth = 50);
    // here's this constructor if you want it, I guess?
    document(std::unique_ptr<node> nodes) : nodes(std::move(nodes)) {}
    node::const_iterator begin() const { return node::const_iterator(nodes.get()); }
    node::const_iterator cbegin() const { return begin(); }
    node::const_iterator end() const { return node::const_iterator(nullptr); }
    node::const_iterator cend() const { return end(); }
  };
}

#endif
