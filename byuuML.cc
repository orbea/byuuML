#include "byuuML.hh"

#include <vector>
#include <list>
#include <algorithm>

using namespace byuuML;

namespace {
  class line_getter : std::vector<char> {
    reader& reader;
    // Pointers to the buffered region. These may either be in the buffer
    // managed by reader, or in our own internal buffer.
    const char* buf_begin = nullptr, *buf_end = nullptr;
    void get_buf_line(const char*& _begin, const char*& end) {
      const char* begin = _begin;
      while(begin != end && (*begin == '\n' || *begin == '\r'))
        ++begin;
      const char* p = begin;
      while(p != end && *p != '\n' && *p != '\r')
        ++p;
      _begin = begin;
      end = p;
    }
    void clean_up() {
      if(buf_begin == nullptr) {
        // No cleanup could possibly be needed.
      }
      else if(size() != 0 && buf_begin == data() + size()) {
        // If the last thing we outputted was a pointer into our buffer, and
        // we have no reason to keep the contents of the buffer, let's go ahead
        // and free all that now.
        clear();
        buf_end = buf_begin = nullptr;
      }
      else if(buf_begin == buf_end) {
        // If the "buffer" is empty, then let's make it easier for the next
        // guy.
        buf_end = buf_begin = nullptr;
      }
    }
    bool read_from_buffer(const char*& out_begin, const char*& out_end) {
      const char* line_begin, *line_end;
      // we may have more buffered
      line_begin = buf_begin;
      line_end = buf_end;
      get_buf_line(line_begin, line_end);
      if(line_end != buf_end) {
        // We still had enough in the buffer to end the line.
        out_begin = line_begin;
        out_end = line_end;
        buf_begin = line_end;
        while(buf_begin != buf_end && (*buf_begin == '\n'
                                      || *buf_begin == '\r'))
          ++buf_begin;
        return true;
      }
      else {
        // We need to buffer more.
        buf_begin = line_begin;
        // Copy the remaining existing buffered data to our internal buffer.
        if(buf_end == data() + size()) {
          if(buf_begin == data()) {
            // The data is already in our buffer, aligned to the beginning.
            // We don't need to do anything.
          }
          else {
            // The data is already in our buffer, but not at the beginning.
            std::move(buf_begin, buf_end, data());
            resize(buf_end - buf_begin);
          }
        }
        else {
          assign(buf_begin, buf_end);
        }
        // buf_begin and buf_end are temporarily invalid
        // Read more data.
        const char* in_begin, *in_end;
        reader.read_more(in_begin, in_end);
        if(in_begin == in_end) {
          // There's no more input. The line we have is IT.
          out_begin = line_begin;
          out_end = line_end;
          buf_end = buf_begin = nullptr;
          return true;
        }
        auto old_size = size();
        resize(size() + (in_end - in_begin));
        std::move(in_begin, in_end, data() + old_size);
        buf_begin = data();
        buf_end = data() + size();
        // we want to go through again
        return false;
      }
    }
  public:
    line_getter(class reader& reader) : reader(reader) {}
    // like reader, begin == end means no more lines
    void get_line(const char*& out_begin, const char*& out_end) {
      while(true) {
        clean_up();
        if(buf_begin == buf_end) {
          reader.read_more(buf_begin, buf_end);
          if(buf_begin == buf_end) {
            // no more lines are coming, buster
            out_end = out_begin = nullptr;
            return;
          }
        }
        while(!read_from_buffer(out_begin, out_end)) {
        }
        if(out_begin != out_end && out_begin + 1 != out_end
           && out_begin[0] == '/' && out_begin[1] == '/') {
          // commented line
          continue;
        }
        else {
          // valid line!
          break;
        }
      }
    }
  };
  struct node_being_parsed {
    std::string name;
    // An open node has a spurious '\n' at the end of its data, IFF it had data
    // specified. This spurious '\n' is removed when the node is closed. This
    // provides the required semantics for Data Continuations.
    std::string data;
    int indentation_level;
    std::list<node_being_parsed> children;
  };
  int parse_indentation(const char*& begin, const char*& end) {
    int ret = 0;
    while(begin != end && (*begin == ' ' || *begin == '\t')) {
      ++begin;
      ++ret;
    }
    return ret;
  }
  constexpr bool is_valid_name_char(char c) {
    if(c >= 'A' && c <= 'Z') return true;
    else if(c >= 'a' && c <= 'z') return true;
    else if(c >= '0' && c <= '9') return true;
    else if(c == '-' || c == '.') return true;
    else return false;
  }
  void close_node(node_being_parsed& dying_node) {
    if(!dying_node.data.empty()) // the last character will be a spurious '\n'
      dying_node.data.resize(dying_node.data.size()-1);
  }
  void parse_node(node_being_parsed& in_node,
                  const char*& _begin, const char*& end,
                  bool is_attribute_node = false) {
    const char* begin = _begin;
    if(!is_valid_name_char(*begin))
      throw std::string("Invalid node name");
    const char* p = begin;
    do {
      ++p;
    } while(p != end && is_valid_name_char(*p));
    in_node.name = std::string(begin, p);
    if(p == end) {
      // No data, no attribute
    }
    else if(*p == '=') {
      ++p;
      if(p != end && *p == '"') {
        // Quoted attribute-style data
        begin = ++p;
        while(p != end && *p != '"') {
          ++p;
        }
        if(p == end)
          throw std::string("Unterminated quoted data");
        // *p == '"'
        in_node.data.assign(begin, p);
        if(!is_attribute_node)
          in_node.data.push_back('\n');
        ++p; // skip the '"'
      }
      else {
        begin = p;
        // Unquoted attribute-style data
        while(p != end && *p != ' ') ++p;
        in_node.data.assign(begin, p);
        if(!is_attribute_node)
          in_node.data.push_back('\n');
      }
      // Attributes may follow
    }
    else if(*p == ':') {
      ++p;
      in_node.data.assign(p, end);
      if(!is_attribute_node)
        in_node.data.push_back('\n');
      p = end;
    }
    else if(*p == ' ') {
      // No data, but attributes may follow
      ++p;
    }
    if(!is_attribute_node) {
      while(p != end) {
        while(p != end && *p == ' ') ++p;
        if(p + 1 != end && p[0] == '/' && p[1] == '/') {
          // comment
          p = end;
          break;
        }
        in_node.children.emplace_back();
        parse_node(*in_node.children.rbegin(), p, end, true);
        // attribute nodes don't need to be closed
      }
    }
    _begin = p;
  }
  void parse_datacont(node_being_parsed& in_node,
                      const char*& begin, const char*& end) {
    // not too efficient with modern string, but Data Continuations are rare.
    in_node.data.insert(in_node.data.end(), begin, end);
    in_node.data.push_back('\n');
  }
  template<class T>
  std::unique_ptr<node> cook_nodes(const T& list) {
    std::unique_ptr<node> next_node;
    for(auto it = list.rbegin(); it != list.rend(); ++it){
      next_node = std::make_unique<node>(it->name, it->data,
                                         cook_nodes(it->children),
                                         std::move(next_node));
    }
    return next_node;
  }
}

document::document(reader& reader, size_t max_depth) {
  line_getter lines(reader);
  std::vector<node_being_parsed> open_nodes;
  std::vector<node_being_parsed> document_nodes;
  open_nodes.reserve(5); // most documents will not be deeper than this
  const char* begin, *end;
  while(lines.get_line(begin, end), begin != end) {
    open_nodes.resize(open_nodes.size()+1);
    auto& in_node = *open_nodes.rbegin();
    in_node.indentation_level = parse_indentation(begin, end);
    if(begin == end) throw std::string("Blank indented line.");
    if(open_nodes.size() == 1) {
      // There are no other open nodes. This *must* be a top-level node.
      if(in_node.indentation_level != 0)
        throw std::string("Indented node has no parent");
      parse_node(in_node, begin, end);
      // we... are done here.
    }
    else {
      // There are other open nodes. We are a child, sibling, or ancestor.
      auto& recent_node = *(open_nodes.rbegin()+1);
      if(in_node.indentation_level < recent_node.indentation_level) {
        // We are an ancestor. Close out nodes until we're not anymore.
        auto n = open_nodes.size() - 1;
        do {
          --n;
          close_node(open_nodes[n]);
          if(n == 0) {
            // closing a root node
            document_nodes.emplace_back(std::move(open_nodes[n]));
            break;
          }
          else {
            // closing a child node
            open_nodes[n-1].children.emplace_back(std::move(open_nodes[n]));
          }
        } while(in_node.indentation_level < open_nodes[n].indentation_level);
        // n is now the level at which we need to exist
        auto& target_node = open_nodes[n];
        if(in_node.indentation_level != target_node.indentation_level)
          throw std::string("Invalid indentation level");
        target_node.children.clear();
        parse_node(target_node, begin, end);
        open_nodes.resize(n+1);
      }
      else if(in_node.indentation_level > recent_node.indentation_level) {
        // We are a child, OR we are a Data Continuation.
        if(begin[0] == ':') {
          parse_datacont(recent_node, ++begin, end);
          open_nodes.resize(open_nodes.size()-1);
        }
        else {
          parse_node(in_node, begin, end);
        }
        // we are done here.
      }
      else {
        // We are a sibling. Close out our previous sibling.
        close_node(recent_node);
        if(open_nodes.size() == 2) {
          // Sibling is a root node.
          document_nodes.emplace_back(std::move(recent_node));
        }
        else {
          // Sibling is a child node.
          open_nodes[open_nodes.size()-3].children.emplace_back(std::move(recent_node));
        }
        recent_node.children.clear();
        parse_node(recent_node, begin, end);
        open_nodes.resize(open_nodes.size()-1);
      }
    }
    if(open_nodes.size() > max_depth) {
      throw std::string("Document too deep");
    }
  }
  if(open_nodes.empty()) {
    // This can only happen if the document is empty.
    throw std::string("Empty document");
  }
  // Close all remaining nodes.
  auto n = open_nodes.size() - 1;
  while(true) {
    close_node(open_nodes[n]);
    if(n == 0) {
      // closing a root node
      document_nodes.emplace_back(std::move(open_nodes[n]));
      break;
    }
    else {
      // closing a child node
      open_nodes[n-1].children.emplace_back(std::move(open_nodes[n]));
      --n;
    }
  }
  // Implementing this without recursion would be more efficient, but much less
  // clear. Recursion will likely be used 
  nodes = cook_nodes(document_nodes);;
}
