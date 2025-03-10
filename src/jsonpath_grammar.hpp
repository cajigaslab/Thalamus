#include <iterator>
#include <sstream>
#include <algorithm>
#include <variant>
#include <vector>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <boost/phoenix/core.hpp>
#include <boost/phoenix/operator.hpp>
#include <boost/phoenix/fusion.hpp>
#include <boost/phoenix/stl.hpp>
#include <boost/foreach.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/fusion/tuple.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/variant/apply_visitor.hpp>
#include <boost/variant/recursive_variant.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif
namespace qi = boost::spirit::qi;
namespace ascii = boost::spirit::ascii;

using Literal = std::variant<int, std::string>;
using SingularQuery = std::vector<std::variant<std::string, int>>;
using CompareArg = std::variant<Literal, SingularQuery>;
//using LogicalExpr = std::vector<std::vector<Compare>>;
//
struct Compare {
  CompareArg lhs;
  std::string op;
  CompareArg rhs;
};
BOOST_FUSION_ADAPT_STRUCT(Compare, lhs, op, rhs);

struct LogicalAnd {
  std::vector<Compare> expressions;
};
BOOST_FUSION_ADAPT_STRUCT(LogicalAnd, expressions);

struct LogicalOr {
  std::vector<LogicalAnd> expressions;
};
BOOST_FUSION_ADAPT_STRUCT(LogicalOr, expressions);

struct Selector {
   std::variant<std::string, int, LogicalOr> value;
};
BOOST_FUSION_ADAPT_STRUCT(Selector, value);

struct ChildSegment {
  std::variant<Selector, std::string> segment;
};
BOOST_FUSION_ADAPT_STRUCT(ChildSegment, segment);
using Segment = ChildSegment;

struct JsonpathQuery {
  std::vector<Segment> segments;
};
BOOST_FUSION_ADAPT_STRUCT(JsonpathQuery, segments);

template <typename Iterator>
struct parser : qi::grammar<Iterator, JsonpathQuery(), ascii::space_type> {
  parser() : parser::base_type(jsonpath_query) {
    using namespace qi::labels;
    using boost::phoenix::at_c;
    using boost::phoenix::push_back;

    name_first = ascii::char_("a-zA-Z_");
    name_char = ascii::char_("a-zA-Z0-9_");
    member_name_shorthand = name_first >> *name_char;
    string_literal = ('\'' >> qi::lexeme[+(qi::char_ - '\'')] >> '\'') | ('"' >> qi::lexeme[+(qi::char_ - '"')] >> '"');
    literal = qi::int_ [_val = _1] | string_literal [_val = _1] | qi::string("true") [_val = _1] | qi::string("false") [_val = _1] | qi::string("null") [_val = _1];
    //logical_not_op = qi::string("!");
    //current_node_identifier = qi::string("@");
    //root_identifier = qi::string("$");

    jsonpath_query = -qi::lit('$') >> *segment [push_back(at_c<0>(_val), _1)];
    segment = child_segment.alias();
    child_segment = bracketed_selection | ('.' >> member_name_shorthand);
    bracketed_selection = '[' >> selector [ _val = _1 ] >> ']';

    selector = name_selector | index_selector | filter_selector;
    name_selector = string_literal;
    index_selector = qi::int_;

    filter_selector = '?' >> logical_expr [ _val = _1 ];
    logical_expr = logical_or_expr.alias();
    logical_or_expr = logical_and_expr [push_back(at_c<0>(_val), _1)] >> *("||" >> logical_and_expr [push_back(at_c<0>(_val), _1)] );
    logical_and_expr = basic_expr [push_back(at_c<0>(_val), _1)] >> *("&&" >> basic_expr [push_back(at_c<0>(_val), _1)] );
    //////basic_expr = paren_expr | comparison_expr | test_expr;
    basic_expr = comparison_expr.alias();
    ////paren_expr = -logical_not_op >> '(' >> logcal_expr >> ')';

    ////test_expr = -'!' >> filter_query;
    ////filter_query = rel_query | jsonpath_query;
    ////rel_query = current_node_identifier | segments;

    comparison_expr = comparable >> comparison_op >> comparable;
    comparable = literal | singular_query;
    comparison_op = qi::string("==") | qi::string("!=") | qi::string("<=") | qi::string(">=") | qi::string("<") | qi::string(">");
    singular_query = rel_singular_query | abs_singular_query;
    rel_singular_query = qi::lit('@') >> singular_query_segments;
    abs_singular_query = qi::lit('$') >> singular_query_segments;
    singular_query_segments = *(name_segment | index_segment);
    name_segment = ('[' >> string_literal >> ']') | ('.' >> member_name_shorthand);
    index_segment = '[' >> index_selector >> ']';
  }

  qi::rule<Iterator, char(), ascii::space_type> name_first;
  qi::rule<Iterator, char(), ascii::space_type> name_char;
  qi::rule<Iterator, std::string(), ascii::space_type> member_name_shorthand;
  qi::rule<Iterator, std::string(), ascii::space_type> string_literal;
  qi::rule<Iterator, Literal(), ascii::space_type> literal;
  //qi::rule<Iterator, std::string(), ascii::space_type> logical_not_op;
  //qi::rule<Iterator, std::string(), ascii::space_type> current_node_identifier;
  //qi::rule<Iterator, std::string(), ascii::space_type> root_identifier;

  qi::rule<Iterator, JsonpathQuery(), ascii::space_type> jsonpath_query;
  qi::rule<Iterator, JsonpathQuery(), ascii::space_type> segments;
  qi::rule<Iterator, Segment(), ascii::space_type> segment;
  qi::rule<Iterator, ChildSegment(), ascii::space_type> child_segment;
  qi::rule<Iterator, Selector(), ascii::space_type> bracketed_selection;

  qi::rule<Iterator, Selector(), ascii::space_type> selector;
  qi::rule<Iterator, std::string(), ascii::space_type> name_selector;
  qi::rule<Iterator, int(), ascii::space_type> index_selector;

  //qi::rule<Iterator, FilterSelector(), ascii::space_type> filter_selector;
  //qi::rule<Iterator, LogicalExpr(), ascii::space_type> logical_expr;
  //qi::rule<Iterator, BasicExpr(), ascii::space_type> basic_expr;
  ////qi::rule<Iterator, ParenExpr, ascii::space_type> paren_expr;

  ////qi::rule<Iterator, TestExpr, ascii::space_type> test_expr;
  ////qi::rule<Iterator, Jsonpath, ascii::space_type> filter_query;
  ////qi::rule<Iterator, Jsonpath, ascii::space_type> rel_query;

  qi::rule<Iterator, LogicalOr(), ascii::space_type> filter_selector;
  qi::rule<Iterator, LogicalOr(), ascii::space_type> logical_expr;
  qi::rule<Iterator, LogicalOr(), ascii::space_type> logical_or_expr;
  qi::rule<Iterator, LogicalAnd(), ascii::space_type> logical_and_expr;
  qi::rule<Iterator, Compare(), ascii::space_type> basic_expr;
  qi::rule<Iterator, Compare(), ascii::space_type> comparison_expr;
  qi::rule<Iterator, CompareArg(), ascii::space_type> comparable;
  qi::rule<Iterator, std::string(), ascii::space_type> comparison_op;
  qi::rule<Iterator, SingularQuery(), ascii::space_type> singular_query;
  qi::rule<Iterator, SingularQuery(), ascii::space_type> rel_singular_query;
  qi::rule<Iterator, SingularQuery(), ascii::space_type> abs_singular_query;
  qi::rule<Iterator, SingularQuery(), ascii::space_type> singular_query_segments;
  qi::rule<Iterator, std::string(), ascii::space_type> name_segment;
  qi::rule<Iterator, int(), ascii::space_type> index_segment;
};
