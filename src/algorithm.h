// C++ std: 20

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct NInfo {
    enum class Type {
        None = 0x00,
        A    = 0x01,
        B    = 0x02,
        AB   = 0x03,
    };

    std::string name;                        // unique id
    Type supported_type;                     // Supported type
    int32_t max_lvl;                         // Maximum level
    int32_t multiplier[2];                   // Positive multipliers for A and B
    std::unordered_set<std::string> matches; // Matches

  private:
    static std::vector<const NInfo> instances; // All instances
    static Type active_type; // Active type, A or B, for switching handling mode

  public:
    static void initialize(const std::vector<NInfo> &info);
    static const std::vector<NInfo> &get_instances();
    static const NInfo &get(int32_t index);
    static const NInfo &get(std::string name);
    static int32_t get_id(std::string name);

    static void set_active_type(Type type);
    static Type get_active_type();
};

struct N {
    const int32_t id;    // A nonnegative number
    mutable int32_t lvl; // A positive number

  private:
    static std::unordered_map<int32_t, std::unordered_set<int32_t>> match_table; // Global match table

  public:
    struct Hash {
        size_t operator()(const N &n) const { return std::hash<int32_t>()(n.id); }
    };

    bool operator==(const N &other) const;

    static void initialize_matches(const std::vector<std::pair<int32_t, int32_t>> &matches);
    static const std::unordered_set<int32_t> &get_matches(int32_t n);
    static bool is_match(int32_t n1, int32_t n2);

    const std::unordered_set<int32_t> &get_matches() const;
    bool is_match(const N &other) const;

    std::string get_name() const;
    NInfo::Type get_type() const;
    int32_t get_max_lvl() const;
    int32_t get_multiplier() const;

    N(int32_t id);
    N(int32_t id, int32_t lvl);
};

class NSet {
  private:
    std::unordered_set<N, N::Hash> elements; // Elements
    std::unordered_set<int32_t> matches;     // All matches of all elements

    void update_matches();

  public:
    void insert(const N &n);
    void remove(int32_t id);
    void clear();

    size_t size() const;
    bool empty() const;
    const N *find(int32_t id) const;

    const std::unordered_set<N, N::Hash> &get_elements() const;
    const std::unordered_set<int32_t> &get_matches() const;

    int match_count(const NSet &b) const;
    bool is_match(const N &other) const;

    int32_t combine(const NSet &other);

    NSet() = default;
    NSet(const std::initializer_list<N> &elems);
    template <std::input_iterator InputIt>
        requires std::convertible_to<std::iter_value_t<InputIt>, N>
    explicit NSet(InputIt first, InputIt last) : elements(first, last) {
        update_matches();
    }
};

int match_count(const NSet &a, const NSet &b);
