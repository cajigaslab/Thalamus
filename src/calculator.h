#define BOOST_SPIRIT_NO_PREDEFINED_TERMINALS

#if defined(_MSC_VER)
# pragma warning(disable: 4345)
#endif

#include <boost/spirit/include/qi.hpp>
#include <boost/variant/recursive_variant.hpp>
#include <boost/variant/apply_visitor.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/foreach.hpp>

#include <iostream>
#include <variant>
#include <string>
#include <map>
#include <util.h>

namespace thalamus { 
  namespace calculator {
    ///////////////////////////////////////////////////////////////////////////
    //  The AST
    ///////////////////////////////////////////////////////////////////////////
    struct nil {};
    struct signed_;
    struct program;
    using number = std::variant<long long int, double>;

    typedef boost::variant<
            nil
          , std::string
          , unsigned long long int
          , double
          , boost::recursive_wrapper<signed_>
          , boost::recursive_wrapper<program>
        >
    operand;

    struct signed_
    {
      std::string sign;
      operand operand_;
    };

    struct operation
    {
      std::string operator_;
      operand operand_;
      std::string operator2_;
      operand operand2_;
    };

    struct program
    {
        operand first;
        std::list<operation> rest;
    };
  }
}

BOOST_FUSION_ADAPT_STRUCT(
    thalamus::calculator::signed_,
    (std::string, sign)
    (thalamus::calculator::operand, operand_)
)

BOOST_FUSION_ADAPT_STRUCT(
    thalamus::calculator::operation,
    (std::string, operator_)
    (thalamus::calculator::operand, operand_)
    (std::string, operator2_)
    (thalamus::calculator::operand, operand2_)
)

BOOST_FUSION_ADAPT_STRUCT(
    thalamus::calculator::program,
    (thalamus::calculator::operand, first)
    (std::list<thalamus::calculator::operation>, rest)
)

namespace thalamus { 
  namespace calculator {
    struct bool_visitor {
      bool operator()(long long int rhs) {
        return rhs;
      }
      bool operator()(double rhs) {
        return rhs;
      }

      bool operator()(const std::variant<long long int, double>& rhs) {
        return std::visit(*this, rhs);
      }
    };

    inline bool to_bool(const std::variant<long long int, double>& rhs) {
      return std::visit(bool_visitor{}, rhs);
    }

    struct eval
    {
        typedef number result_type;
        std::map<std::string, number> symbols;

        number operator()(nil) const { THALAMUS_ASSERT(false); return 0; }
        number operator()(double n) const { return n; }
        number operator()(unsigned long long int n) const { return (long long int)n; }
        number operator()(std::string n) const { return symbols.at(n); }

        template<typename LHS, typename RHS>
        number eval_binary(operation const& x, LHS lhs, RHS rhs) const {
            if(x.operator_ == "+") {
              return lhs + rhs;
            } else if(x.operator_ == "-") {
              return lhs - rhs;
            } else if(x.operator_ == "*") {
              return lhs * rhs;
            } else if(x.operator_ == "/") {
              return lhs / rhs;
            } else if(x.operator_ == "=") {
              return lhs == rhs ? 1ll : 0ll;
            } else if(x.operator_ == "<>") {
              return lhs != rhs ? 1ll : 0ll;
            } else if(x.operator_ == ">=") {
              return lhs >= rhs ? 1ll : 0ll;
            } else if(x.operator_ == "<=") {
              return lhs <= rhs ? 1ll : 0ll;
            } else if(x.operator_ == ">") {
              return lhs > rhs ? 1ll : 0ll;
            } else if(x.operator_ == "<") {
              return lhs < rhs ? 1ll : 0ll;
            } else if(x.operator_ == "&&") {
              return lhs && rhs ? 1ll : 0ll;
            } else if(x.operator_ == "||") {
              return lhs || rhs ? 1ll : 0ll;
            }
            if constexpr(std::is_integral<LHS>() && std::is_integral<RHS>()) {
              if(x.operator_ == "|") {
                return lhs | rhs;
              } else if(x.operator_ == "&") {
                return lhs & rhs;
              } else if(x.operator_ == ">>") {
                return lhs >> rhs;
              } else if(x.operator_ == "<<") {
                return lhs << rhs;
              } else if(x.operator_ == "%") {
                return lhs % rhs;
              }
            }
            BOOST_ASSERT(0);
            return 0ll;
        }

        number eval_binary_var(operation const& x, number lhs, number rhs) const {
          auto lhs_int = std::holds_alternative<long long int>(lhs);
          auto rhs_int = std::holds_alternative<long long int>(rhs);
          if(lhs_int && rhs_int) {
            return eval_binary(x, std::get<long long int>(lhs), std::get<long long int>(rhs));
          } else if (lhs_int) {
            return eval_binary(x, std::get<long long int>(lhs), std::get<double>(rhs));
          } else if (rhs_int) {
            return eval_binary(x, std::get<double>(lhs), std::get<long long int>(rhs));
          }
          return eval_binary(x, std::get<double>(lhs), std::get<double>(rhs));
        }

        number operator()(operation const& x, number lhs) const
        {
            if(x.operator_ == "?") {
              if(to_bool(lhs)) {
                number rhs = boost::apply_visitor(*this, x.operand_);
                return rhs;
              }
              number rhs2 = boost::apply_visitor(*this, x.operand2_);
              return rhs2;
            }

            number rhs = boost::apply_visitor(*this, x.operand_);
            return eval_binary_var(x, lhs, rhs);
        }


        number operator()(signed_ const& x) const
        {
            number rhs = boost::apply_visitor(*this, x.operand_);
            if(x.sign == "+") {
              if(std::holds_alternative<long long int>(rhs)) {
                return +std::get<long long int>(rhs);
              }
              return +std::get<double>(rhs);
            } else if(x.sign == "-") {
              if(std::holds_alternative<long long int>(rhs)) {
                return -std::get<long long int>(rhs);
              }
              return -std::get<double>(rhs);
            } else if(x.sign == "~") {
              if(std::holds_alternative<long long int>(rhs)) {
                return ~std::get<long long int>(rhs) & 0xFFFFFFFF;
              }
            }
            BOOST_ASSERT(0);
            return 0ll;
        }

        number operator()(program const& x) const
        {
            number state = boost::apply_visitor(*this, x.first);
            BOOST_FOREACH(operation const& oper, x.rest)
            {
                state = (*this)(oper, state);
            }
            return state;
        }
    };

    namespace qi = boost::spirit::qi;
    namespace ascii = boost::spirit::ascii;

    ///////////////////////////////////////////////////////////////////////////////
    //  The calculator grammar
    ///////////////////////////////////////////////////////////////////////////////
    template <typename Iterator>
    struct parser : qi::grammar<Iterator, program(), ascii::space_type>
    {
        parser() : parser::base_type(expression1)
        {
            qi::ulong_long_type ulong_;
            qi::real_parser<double, qi::strict_real_policies<double>> double_;
            qi::uint_parser<unsigned long long int, 16> hex_;
            qi::hex_type hex2_;
            qi::string_type char_;
            qi::char_type one_char_;
            qi::alpha_type alpha_;
            qi::alnum_type alnum_;
            qi::lit_type lit_;
            qi::lexeme_type lexeme_;
            qi::raw_type raw_;

            expression1 =
                boolean
                >> *( char_("?") >> boolean >> char_(":") >> boolean)
                ;

            boolean =
                expression2
                >> *(   (char_("||") >> expression2)
                    |   (char_("&&") >> expression2)
                    )
                ;

            expression2 =
                compare
                >> *(   (char_("|") >> compare)
                    |   (char_("&") >> compare)
                    )
                ;

            compare =
                shift
                >> *(   (char_("=") >> shift)
                    |   (char_("<>") >> shift)
                    |   (char_("<") >> shift)
                    |   (char_(">") >> shift)
                    |   (char_("<=") >> shift)
                    |   (char_(">=") >> shift)
                    )
                ;

            shift =
                expression3
                >> *(   (char_("<<") >> expression3)
                    |   (char_(">>") >> expression3)
                    )
                ;

            expression3 =
                term
                >> *(   (char_("+") >> term)
                    |   (char_("-") >> term)
                    )
                ;

            term =
                factor
                >> *(   (char_("*") >> factor)
                    |   (char_("/") >> factor)
                    |   (char_("%") >> factor)
                    )
                ;

            factor =
                    raw_[lexeme_[(alpha_ | '_') >> *(alnum_ | '_')]] | ("0x" >> hex_) |
                    double_ | ulong_
                |   '(' >> expression1 >> ')'
                |   (char_("-") >> factor)
                |   (char_("+") >> factor)
                |   (char_("~") >> factor)
                ;
        }

        qi::rule<Iterator, program(), ascii::space_type> expression1;
        qi::rule<Iterator, program(), ascii::space_type> boolean;
        qi::rule<Iterator, program(), ascii::space_type> expression2;
        qi::rule<Iterator, program(), ascii::space_type> compare;
        qi::rule<Iterator, program(), ascii::space_type> shift;
        qi::rule<Iterator, program(), ascii::space_type> expression3;
        qi::rule<Iterator, program(), ascii::space_type> term;
        qi::rule<Iterator, operand(), ascii::space_type> factor;
    };
  }
}
