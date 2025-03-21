#include <array>
#include <cinttypes>
#include <iomanip>
#include <ios>
#include <map>
#include <sstream>
#include <stdio.h>
#include <string>
#include <vector>

#include <roaring/art/art.h>
#include <roaring/memory.h>

#include "test.h"

using namespace roaring::internal;

namespace {

void print_key(const art_key_chunk_t* key) {
    for (size_t i = 0; i < ART_KEY_BYTES; ++i) {
        printf("%02x", *(key + i));
    }
}

void assert_key_eq(const art_key_chunk_t* key1, const art_key_chunk_t* key2) {
    for (size_t i = 0; i < ART_KEY_BYTES; ++i) {
        if (*(key1 + i) != *(key2 + i)) {
            print_key(key1);
            printf(" != ");
            print_key(key2);
            printf("\n");

            fail();
        }
    }
}

void assert_art_valid(art_t* art) {
    const char* reason = nullptr;
    if (!art_internal_validate(art, &reason, nullptr, nullptr)) {
        fail_msg("ART is invalid: '%s'\n", reason);
    }
}

class Key {
   public:
    Key(uint64_t key) {
        // Store the low 6 bytes of the key in big-endian order.
        key_[0] = key >> 40 & 0xFF;
        key_[1] = key >> 32 & 0xFF;
        key_[2] = key >> 24 & 0xFF;
        key_[3] = key >> 16 & 0xFF;
        key_[4] = key >> 8 & 0xFF;
        key_[5] = key >> 0 & 0xFF;
    }

    Key(const uint8_t* key) {
        for (size_t i = 0; i < 6; ++i) {
            key_[i] = *(key + i);
        }
    }

    bool operator==(const Key& other) const { return key_ == other.key_; }
    bool operator!=(const Key& other) const { return !(*this == other); }
    bool operator<(const Key& other) const { return key_ < other.key_; }
    bool operator>(const Key& other) const { return key_ > other.key_; }

    const uint8_t* data() const { return key_.data(); }

    std::string string() const {
        std::stringstream os;
        os << std::hex << std::setfill('0');
        for (size_t i = 0; i < 6; ++i) {
            os << std::setw(2) << static_cast<int>(key_[i]) << " ";
        }
        return os.str();
    }

   private:
    std::array<uint8_t, 6> key_;
};

class ShadowedART {
   public:
    ShadowedART() { art_init_cleared(&art_); }
    ~ShadowedART() { art_free(&art_); }

    void insert(Key key, art_val_t value) {
        shadow_[key] = value;
        art_insert(&art_, key.data(), shadow_[key]);
    }

    void erase(Key key) {
        bool erased = art_erase(&art_, key.data(), nullptr);
        bool shadow_erased = shadow_.erase(key) == 1;
        assert_true(erased == shadow_erased);
    }

    void assertLowerBoundValid(Key key) {
        auto shadow_it = shadow_.lower_bound(key);
        auto art_it = art_lower_bound(&art_, key.data());
        assertIteratorValid(shadow_it, &art_it);
    }

    void assertUpperBoundValid(Key key) {
        auto shadow_it = shadow_.upper_bound(key);
        auto art_it = art_upper_bound(&art_, key.data());
        assertIteratorValid(shadow_it, &art_it);
    }

    void assertValid() {
        for (const auto& entry : shadow_) {
            auto& key = entry.first;
            auto& value = entry.second;
            art_val_t* found_val = art_find(&art_, key.data());
            if (found_val == nullptr) {
                printf("Key %s is not null in shadow but null in ART\n",
                       key.string().c_str());
                assert_true(found_val != nullptr);
                break;
            }
            if (*found_val != value) {
                printf("Key %s: ART value %" PRIu64 " != shadow value %" PRIu64
                       "\n",
                       key.string().c_str(), *found_val, value);
                assert_true(*found_val == value);
                break;
            }
        }
    }

   private:
    void assertIteratorValid(std::map<Key, art_val_t>::iterator& shadow_it,
                             art_iterator_t* art_it) {
        if (shadow_it != shadow_.end() && art_it->value == nullptr) {
            printf("Iterator for key %s is null\n",
                   shadow_it->first.string().c_str());
            assert_true(art_it->value != nullptr);
        }
        if (shadow_it == shadow_.end() && art_it->value != nullptr) {
            printf("Iterator is not null\n");
            assert_true(art_it->value == nullptr);
        }
        if (shadow_it != shadow_.end() &&
            shadow_it->first != Key(art_it->key)) {
            printf("Shadow iterator key = %s, ART key = %s\n",
                   shadow_it->first.string().c_str(),
                   Key(art_it->key).string().c_str());
            assert_true(shadow_it->first == Key(art_it->key));
        }
    }
    std::map<Key, art_val_t> shadow_;
    art_t art_;
};

DEFINE_TEST(test_art_simple) {
    std::vector<const char*> keys = {
        "000001", "000002", "000003", "000004", "001005",
    };
    std::vector<art_val_t> values = {1, 2, 3, 4, 5};

    art_t art;
    art_init_cleared(&art);
    for (size_t i = 0; i < keys.size(); ++i) {
        art_insert(&art, (art_key_chunk_t*)keys[i], values[i]);
    }
    art_val_t found_val = *art_find(&art, (uint8_t*)keys[0]);
    assert_true(found_val == values[0]);
    art_val_t erased_val;
    assert_true(art_erase(&art, (uint8_t*)keys[0], &erased_val));
    assert_true(erased_val == values[0]);
    art_free(&art);
}

DEFINE_TEST(test_art_erase_all) {
    std::vector<const char*> keys = {"000001", "000002"};
    std::vector<art_val_t> values = {1, 2};

    art_t art;
    art_init_cleared(&art);
    art_insert(&art, (uint8_t*)keys[0], values[0]);
    art_insert(&art, (uint8_t*)keys[1], values[1]);
    assert_art_valid(&art);

    art_val_t erased_val1;
    art_val_t erased_val2;
    assert_true(art_erase(&art, (uint8_t*)keys[0], &erased_val1));
    assert_true(art_erase(&art, (uint8_t*)keys[1], &erased_val2));
    assert_true(erased_val1 == values[0]);
    assert_true(erased_val2 == values[1]);

    assert_art_valid(&art);
    art_free(&art);
}

DEFINE_TEST(test_art_is_empty) {
    std::vector<const char*> keys = {
        "000001", "000002", "000003", "000004", "001005",
    };
    std::vector<art_val_t> values = {1, 2, 3, 4, 5};

    art_t art;
    art_init_cleared(&art);
    assert_art_valid(&art);
    assert_true(art_is_empty(&art));
    const char* key = "000001";
    art_insert(&art, (art_key_chunk_t*)key, 1);
    assert_art_valid(&art);
    assert_false(art_is_empty(&art));
    art_free(&art);
}

DEFINE_TEST(test_art_iterator_next) {
    {
        // ART with multiple node sizes.
        std::vector<std::array<uint8_t, 6>> keys;
        std::vector<art_val_t> values;
        std::vector<size_t> sizes = {4, 16, 48, 256};
        for (size_t i = 0; i < sizes.size(); i++) {
            size_t size = sizes[i];
            for (size_t j = 0; j < size; j++) {
                keys.push_back({0, 0, 0, static_cast<uint8_t>(i),
                                static_cast<uint8_t>(j)});
                values.push_back(i * j);
            }
        }
        art_t art;
        art_init_cleared(&art);
        for (size_t i = 0; i < keys.size(); ++i) {
            art_insert(&art, (art_key_chunk_t*)keys[i].data(), values[i]);
            assert_art_valid(&art);
        }

        art_iterator_t iterator = art_init_iterator(&art, true);
        size_t i = 0;
        do {
            assert_key_eq(iterator.key, (art_key_chunk_t*)keys[i].data());
            assert_true(*iterator.value == values[i]);
            ++i;
        } while (art_iterator_next(&iterator));
        art_free(&art);
    }
    {
        // Max-depth ART.
        std::vector<std::array<uint8_t, 6>> keys{
            {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 1}, {0, 0, 0, 0, 1, 0},
            {0, 0, 0, 1, 0, 0}, {0, 0, 1, 0, 0, 0}, {0, 1, 0, 0, 0, 0},
            {1, 0, 0, 0, 0, 0},
        };
        std::vector<art_val_t> values = {0, 1, 2, 3, 4, 5, 6};
        art_t art;
        art_init_cleared(&art);
        for (size_t i = 0; i < keys.size(); ++i) {
            art_insert(&art, (art_key_chunk_t*)keys[i].data(), values[i]);
            assert_art_valid(&art);
        }

        art_iterator_t iterator = art_init_iterator(&art, true);
        size_t i = 0;
        do {
            assert_key_eq(iterator.key, (art_key_chunk_t*)keys[i].data());
            assert_true(*iterator.value == values[i]);
            ++i;
        } while (art_iterator_next(&iterator));
        art_free(&art);
    }
}

DEFINE_TEST(test_art_iterator_prev) {
    {
        // ART with multiple node sizes.
        std::vector<std::array<uint8_t, 6>> keys;
        std::vector<art_val_t> values;
        std::vector<size_t> sizes = {4, 16, 48, 256};
        for (size_t i = 0; i < sizes.size(); i++) {
            uint8_t size = static_cast<uint8_t>(sizes[i]);
            for (size_t j = 0; j < size; j++) {
                keys.push_back({0, 0, 0, static_cast<uint8_t>(i),
                                static_cast<uint8_t>(j)});
                values.push_back(static_cast<uint64_t>(i) * j);
            }
        }
        art_t art;
        art_init_cleared(&art);
        for (size_t i = 0; i < keys.size(); ++i) {
            art_insert(&art, (art_key_chunk_t*)keys[i].data(), values[i]);
            assert_art_valid(&art);
        }

        art_iterator_t iterator = art_init_iterator(&art, /*first=*/false);
        size_t i = keys.size() - 1;
        do {
            assert_key_eq(iterator.key, (art_key_chunk_t*)keys[i].data());
            --i;
        } while (art_iterator_prev(&iterator));
        art_free(&art);
    }
    {
        // Max-depth ART.
        std::vector<std::array<uint8_t, 6>> keys{
            {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 1}, {0, 0, 0, 0, 1, 0},
            {0, 0, 0, 1, 0, 0}, {0, 0, 1, 0, 0, 0}, {0, 1, 0, 0, 0, 0},
            {1, 0, 0, 0, 0, 0},
        };
        std::vector<art_val_t> values = {0, 1, 2, 3, 4, 5, 6};
        art_t art;
        art_init_cleared(&art);
        for (size_t i = 0; i < keys.size(); ++i) {
            art_insert(&art, (art_key_chunk_t*)keys[i].data(), values[i]);
            assert_art_valid(&art);
        }

        art_iterator_t iterator = art_init_iterator(&art, /*first=*/false);
        size_t i = keys.size() - 1;
        do {
            assert_key_eq(iterator.key, (art_key_chunk_t*)keys[i].data());
            assert_true(*iterator.value == values[i]);
            --i;
        } while (art_iterator_prev(&iterator));
        art_free(&art);
    }
}

DEFINE_TEST(test_art_iterator_lower_bound) {
    {
        art_t art;
        art_init_cleared(&art);
        art_iterator_t iterator = art_init_iterator(&art, true);
        assert_null(iterator.value);
        assert_false(
            art_iterator_lower_bound(&iterator, (art_key_chunk_t*)"000000"));
        assert_false(
            art_iterator_lower_bound(&iterator, (art_key_chunk_t*)"000001"));
        art_free(&art);
    }
    {
        std::vector<const char*> keys = {
            "000001", "000002", "000003", "000004", "001005",
        };
        std::vector<art_val_t> values = {1, 2, 3, 4, 5};
        art_t art;
        art_init_cleared(&art);
        for (size_t i = 0; i < keys.size(); ++i) {
            art_insert(&art, (art_key_chunk_t*)keys[i], values[i]);
            assert_art_valid(&art);
        }

        art_iterator_t iterator = art_init_iterator(&art, true);
        assert_true(
            art_iterator_lower_bound(&iterator, (art_key_chunk_t*)keys[2]));
        assert_key_eq(iterator.key, (art_key_chunk_t*)keys[2]);
        const char* key = "000005";
        assert_true(art_iterator_lower_bound(&iterator, (art_key_chunk_t*)key));
        assert_key_eq(iterator.key, (art_key_chunk_t*)keys[4]);
        art_free(&art);
    }
    {
        // Lower bound search within a node's children.
        std::vector<const char*> keys = {"000001", "000003", "000004",
                                         "001005"};
        std::vector<art_val_t> values = {1, 3, 4, 5};
        art_t art;
        art_init_cleared(&art);
        for (size_t i = 0; i < keys.size(); ++i) {
            art_insert(&art, (art_key_chunk_t*)keys[i], values[i]);
            assert_art_valid(&art);
        }
        art_iterator_t iterator = art_init_iterator(&art, true);

        const char* key1 = "000002";
        assert_true(
            art_iterator_lower_bound(&iterator, (art_key_chunk_t*)key1));
        assert_key_eq(iterator.key, (art_key_chunk_t*)keys[1]);

        // Check that we can go backward within a node's children.
        const char* key2 = "000001";
        assert_true(
            art_iterator_lower_bound(&iterator, (art_key_chunk_t*)key2));
        assert_key_eq(iterator.key, (art_key_chunk_t*)keys[0]);

        art_free(&art);
    }
    {
        // Lower bound search with leaf where prefix is equal but full key is
        // smaller.
        std::vector<const char*> keys = {"000100", "000200", "000300"};
        std::vector<art_val_t> values = {1, 2, 3};
        art_t art;
        art_init_cleared(&art);
        for (size_t i = 0; i < keys.size(); ++i) {
            art_insert(&art, (art_key_chunk_t*)keys[i], values[i]);
            assert_art_valid(&art);
        }
        art_iterator_t iterator = art_init_iterator(&art, true);

        {
            const char* key = "000201";
            assert_true(
                art_iterator_lower_bound(&iterator, (art_key_chunk_t*)key));
            assert_key_eq(iterator.key, (art_key_chunk_t*)keys[2]);
        }
        {
            // Check that we can go backward.
            const char* key = "000099";
            assert_true(
                art_iterator_lower_bound(&iterator, (art_key_chunk_t*)key));
            assert_key_eq(iterator.key, (art_key_chunk_t*)keys[0]);
        }
        {
            // Check that we can go backward from after the end.
            const char* key = "000300";
            assert_true(
                art_iterator_lower_bound(&iterator, (art_key_chunk_t*)key));
            assert_key_eq(iterator.key, (art_key_chunk_t*)keys[2]);
            assert_false(art_iterator_next(&iterator));
            assert_true(
                art_iterator_lower_bound(&iterator, (art_key_chunk_t*)key));
            assert_key_eq(iterator.key, (art_key_chunk_t*)keys[2]);
        }
        {
            // Check that we can go forward from before the start.
            const char* key = "000100";
            assert_true(
                art_iterator_lower_bound(&iterator, (art_key_chunk_t*)key));
            assert_key_eq(iterator.key, (art_key_chunk_t*)keys[0]);
            assert_false(art_iterator_prev(&iterator));
            assert_true(
                art_iterator_lower_bound(&iterator, (art_key_chunk_t*)key));
            assert_key_eq(iterator.key, (art_key_chunk_t*)keys[0]);
        }

        art_free(&art);
    }
    {
        // Lower bound search with only a single leaf.
        const char* key1 = "000001";
        art_val_t value{1};
        art_t art;
        art_init_cleared(&art);
        art_insert(&art, (art_key_chunk_t*)key1, value);

        art_iterator_t iterator = art_init_iterator(&art, true);

        assert_true(
            art_iterator_lower_bound(&iterator, (art_key_chunk_t*)key1));
        assert_key_eq(iterator.key, (art_key_chunk_t*)key1);

        const char* key2 = "000000";
        assert_true(
            art_iterator_lower_bound(&iterator, (art_key_chunk_t*)key2));
        assert_key_eq(iterator.key, (art_key_chunk_t*)key1);

        const char* key3 = "000002";
        assert_false(
            art_iterator_lower_bound(&iterator, (art_key_chunk_t*)key3));

        art_free(&art);
    }
}

DEFINE_TEST(test_art_lower_bound) {
    std::vector<const char*> keys = {
        "000001", "000002", "000003", "000004", "001005",
    };
    std::vector<art_val_t> values = {1, 2, 3, 4, 5};
    art_t art;
    art_init_cleared(&art);
    for (size_t i = 0; i < keys.size(); ++i) {
        art_insert(&art, (art_key_chunk_t*)keys[i], values[i]);
        assert_art_valid(&art);
    }

    {
        const char* key = "000002";
        art_iterator_t iterator = art_lower_bound(&art, (art_key_chunk_t*)key);
        size_t i = 1;
        do {
            assert_true(iterator.value != NULL);
            assert_key_eq(iterator.key, (art_key_chunk_t*)keys[i]);
            assert_true(*iterator.value == values[i]);
            ++i;
        } while (art_iterator_next(&iterator));
    }
    {
        const char* key = "000005";
        art_iterator_t iterator = art_lower_bound(&art, (art_key_chunk_t*)key);
        assert_true(iterator.value != NULL);
        assert_key_eq(iterator.key, (art_key_chunk_t*)keys[4]);
        assert_true(*iterator.value == values[4]);
        assert_false(art_iterator_next(&iterator));
    }
    {
        const char* key = "001006";
        art_iterator_t iterator = art_lower_bound(&art, (art_key_chunk_t*)key);
        assert_true(iterator.value == NULL);
    }
    art_free(&art);
}

DEFINE_TEST(test_art_upper_bound) {
    std::vector<const char*> keys = {
        "000001", "000002", "000003", "000004", "001005",
    };
    std::vector<art_val_t> values = {1, 2, 3, 4, 5};
    art_t art;
    art_init_cleared(&art);
    for (size_t i = 0; i < keys.size(); ++i) {
        art_insert(&art, (art_key_chunk_t*)keys[i], values[i]);
        assert_art_valid(&art);
    }

    {
        const char* key = "000002";
        art_iterator_t iterator = art_upper_bound(&art, (art_key_chunk_t*)key);
        size_t i = 2;
        do {
            assert_true(iterator.value != NULL);
            assert_key_eq(iterator.key, (art_key_chunk_t*)keys[i]);
            assert_true(*iterator.value == values[i]);
            ++i;
        } while (art_iterator_next(&iterator));
    }
    {
        const char* key = "000005";
        art_iterator_t iterator = art_upper_bound(&art, (art_key_chunk_t*)key);
        assert_true(iterator.value != NULL);
        assert_key_eq(iterator.key, (art_key_chunk_t*)keys[4]);
        assert_true(*iterator.value == values[4]);
        assert_false(art_iterator_next(&iterator));
    }
    {
        const char* key = "001006";
        art_iterator_t iterator = art_upper_bound(&art, (art_key_chunk_t*)key);
        assert_true(iterator.value == NULL);
    }
    art_free(&art);
}

DEFINE_TEST(test_art_iterator_erase) {
    std::vector<std::array<uint8_t, 6>> keys;
    std::vector<art_val_t> values;
    std::vector<size_t> sizes = {1, 4, 16, 48, 256};
    for (size_t i = 0; i < sizes.size(); i++) {
        uint8_t size = static_cast<uint8_t>(sizes[i]);
        for (size_t j = 0; j < size; j++) {
            keys.push_back(
                {0, 0, 0, static_cast<uint8_t>(i), static_cast<uint8_t>(j)});
            values.push_back(static_cast<uint64_t>(i) * j);
        }
    }
    art_t art;
    art_init_cleared(&art);
    for (size_t i = 0; i < keys.size(); ++i) {
        art_insert(&art, (art_key_chunk_t*)keys[i].data(), values[i]);
        assert_art_valid(&art);
    }
    art_iterator_t iterator = art_init_iterator(&art, true);
    size_t i = 0;
    do {
        assert_key_eq(iterator.key, (art_key_chunk_t*)keys[i].data());
        assert_true(*iterator.value == values[i]);
        art_val_t erased_val;
        assert_true(art_iterator_erase(&iterator, &erased_val));
        assert_true(erased_val == values[i]);
        assert_art_valid(&art);
        assert_false(art_find(&art, (art_key_chunk_t*)keys[i].data()));
        ++i;
    } while (iterator.value != NULL);
    assert_true(i == values.size());
    art_free(&art);
}

DEFINE_TEST(test_art_iterator_insert) {
    std::vector<const char*> keys = {
        "000001", "000002", "000003", "000004", "001005",
    };
    std::vector<art_val_t> values = {1, 2, 3, 4, 5};
    art_t art;
    art_init_cleared(&art);
    art_insert(&art, (art_key_chunk_t*)keys[0], values[0]);
    art_iterator_t iterator = art_init_iterator(&art, true);
    for (size_t i = 1; i < keys.size(); ++i) {
        art_iterator_insert(&iterator, (art_key_chunk_t*)keys[i], values[i]);
        assert_art_valid(&art);
        assert_key_eq(iterator.key, (art_key_chunk_t*)keys[i]);
        assert_true(*iterator.value == values[i]);
    }
    art_free(&art);
}

DEFINE_TEST(test_art_shadowed) {
    ShadowedART art;
    for (uint64_t i = 0; i < 10000; ++i) {
        art.insert(i, i);
    }
    art.assertValid();
    art.assertLowerBoundValid(5000);
    art.assertLowerBoundValid(10000);
    for (uint64_t i = 0; i < 10000; ++i) {
        art.erase(i);
    }
    art.assertValid();
    art.assertLowerBoundValid(1);
}

DEFINE_TEST(test_art_shrink_grow_node48) {
    art_t art;
    art_init_cleared(&art);
    std::vector<art_val_t> values(48);
    // Make a full node48.
    for (int i = 0; i < 48; i++) {
        auto key = Key(i);
        values[i] = i;
        art_insert(&art, key.data(), values[i]);
        assert_art_valid(&art);
    }
    // Remove the first several containers
    for (int i = 0; i < 8; i++) {
        auto key = Key(i);
        art_val_t erased_val;
        assert_true(art_erase(&art, key.data(), &erased_val));
        assert_art_valid(&art);
        assert_int_equal(erased_val, i);
    }
    {
        art_iterator_t iterator = art_init_iterator(&art, true);
        int i = 8;
        do {
            auto key = Key(i);
            assert_key_eq(iterator.key, key.data());
            assert_true(*iterator.value == values[i]);
            ++i;
        } while (art_iterator_next(&iterator));
        assert_int_equal(i, 48);
    }

    // Fill the containers back up
    for (int i = 0; i < 8; i++) {
        auto key = Key(i);
        values[i] = i;
        art_insert(&art, key.data(), values[i]);
    }
    {
        art_iterator_t iterator = art_init_iterator(&art, true);
        int i = 0;
        do {
            auto key = Key(i);
            assert_key_eq(iterator.key, key.data());
            assert_true(*iterator.value == values[i]);
            ++i;
        } while (art_iterator_next(&iterator));
        assert_int_equal(i, 48);
    }
    art_free(&art);
}

DEFINE_TEST(test_art_frozen_view) {
    {
        // ART with multiple node sizes.
        std::vector<std::array<uint8_t, 6>> keys;
        std::vector<art_val_t> values;
        std::vector<size_t> sizes = {4, 16, 48, 256};
        for (size_t i = 0; i < sizes.size(); i++) {
            size_t size = sizes[i];
            for (size_t j = 0; j < size; j++) {
                keys.push_back({0, 0, 0, static_cast<uint8_t>(i),
                                static_cast<uint8_t>(j)});
                values.push_back(i * j);
            }
        }
        art_t art1;
        art_init_cleared(&art1);
        for (size_t i = 0; i < keys.size(); ++i) {
            art_insert(&art1, (art_key_chunk_t*)keys[i].data(), values[i]);
            assert_art_valid(&art1);
        }

        art_shrink_to_fit(&art1);
        size_t serialized_size = art_size_in_bytes(&art1);
        char* buf = (char*)roaring_aligned_malloc(8, serialized_size);
        assert_int_equal(art_serialize(&art1, buf), serialized_size);
        art_free(&art1);

        art_t art2;
        assert_int_equal(art_frozen_view(buf, serialized_size, &art2),
                         serialized_size);

        art_iterator_t iterator = art_init_iterator(&art2, true);
        size_t i = 0;
        do {
            assert_key_eq(iterator.key, (art_key_chunk_t*)keys[i].data());
            assert_true(*iterator.value == values[i]);
            ++i;
        } while (art_iterator_next(&iterator));
        roaring_aligned_free(buf);
    }
    {
        // Max-depth ART.
        std::vector<std::array<uint8_t, 6>> keys{
            {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 1}, {0, 0, 0, 0, 1, 0},
            {0, 0, 0, 1, 0, 0}, {0, 0, 1, 0, 0, 0}, {0, 1, 0, 0, 0, 0},
            {1, 0, 0, 0, 0, 0},
        };
        std::vector<art_val_t> values = {0, 1, 2, 3, 4, 5, 6};
        art_t art1;
        art_init_cleared(&art1);
        for (size_t i = 0; i < keys.size(); ++i) {
            art_insert(&art1, (art_key_chunk_t*)keys[i].data(), values[i]);
            assert_art_valid(&art1);
        }

        art_shrink_to_fit(&art1);
        size_t serialized_size = art_size_in_bytes(&art1);
        char* buf = (char*)roaring_aligned_malloc(8, serialized_size);
        assert_int_equal(art_serialize(&art1, buf), serialized_size);
        art_free(&art1);

        art_t art2;
        assert_int_equal(art_frozen_view(buf, serialized_size, &art2),
                         serialized_size);

        art_iterator_t iterator = art_init_iterator(&art2, true);
        size_t i = 0;
        do {
            assert_key_eq(iterator.key, (art_key_chunk_t*)keys[i].data());
            assert_true(*iterator.value == values[i]);
            ++i;
        } while (art_iterator_next(&iterator));
        roaring_aligned_free(buf);
    }
}

}  // namespace

int main() {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_art_simple),
        cmocka_unit_test(test_art_erase_all),
        cmocka_unit_test(test_art_is_empty),
        cmocka_unit_test(test_art_iterator_next),
        cmocka_unit_test(test_art_iterator_prev),
        cmocka_unit_test(test_art_iterator_lower_bound),
        cmocka_unit_test(test_art_lower_bound),
        cmocka_unit_test(test_art_upper_bound),
        cmocka_unit_test(test_art_iterator_erase),
        cmocka_unit_test(test_art_iterator_insert),
        cmocka_unit_test(test_art_shadowed),
        cmocka_unit_test(test_art_shrink_grow_node48),
        cmocka_unit_test(test_art_frozen_view),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
