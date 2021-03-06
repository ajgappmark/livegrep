/********************************************************************
 * livegrep -- codesearch.h
 * Copyright (c) 2011-2013 Nelson Elhage
 *
 * This program is free software. You may use, redistribute, and/or
 * modify it under the terms listed in the COPYING file.
 ********************************************************************/
#ifndef CODESEARCH_H
#define CODESEARCH_H

#include <vector>
#include <string>
#include <map>
#include <fstream>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>
#include <boost/intrusive_ptr.hpp>

#ifdef USE_DENSE_HASH_SET
#include <google/dense_hash_set>
#else
#include <google/sparse_hash_set>
#endif
#include <google/sparse_hash_map>
#include "re2/re2.h"
#include <locale>

#include "src/lib/thread_queue.h"

class searcher;
class chunk_allocator;
class file_contents;
struct match_result;

using re2::RE2;
using re2::StringPiece;

using std::string;
using std::locale;
using std::vector;
using std::map;
using std::pair;
using std::atomic_int;

/*
 * We special-case data() == NULL to provide an "empty" element for
 * dense_hash_set.
 *
 * StringPiece::operator== will consider a zero-length string equal to a
 * zero-length string with a NULL data().
 */
struct eqstr {
    bool operator()(const StringPiece& lhs, const StringPiece& rhs) const;
};

struct hashstr {
    locale loc;
    size_t operator()(const StringPiece &str) const;
};

#ifdef USE_DENSE_HASH_SET
typedef google::dense_hash_set<StringPiece, hashstr, eqstr> string_hash;
#else
typedef google::sparse_hash_set<StringPiece, hashstr, eqstr> string_hash;
#endif

enum exit_reason {
    kExitNone = 0,
    kExitTimeout,
    kExitMatchLimit,
};


struct match_stats {
    timeval re2_time;
    timeval git_time;
    timeval sort_time;
    timeval index_time;
    timeval analyze_time;
    int matches;
    exit_reason why;
};

struct chunk;
struct chunk_file;
struct json_object;

struct indexed_tree {
    string name;
    json_object *metadata;
    string version;
};

struct indexed_file {
    const indexed_tree *tree;
    string path;
    file_contents *content;
    int no;
};

struct index_info {
    std::string name;
    vector<indexed_tree> trees;
};

struct match_result {
    indexed_file *file;
    int lno;
    vector<StringPiece> context_before;
    vector<StringPiece> context_after;
    StringPiece line;
    int matchleft, matchright;
};

// A query specification passed to match(). line_pat is required to be
// non-NULL; file_pat, tree_pat and tag_pat may be NULL to specify "no
// constraint"
struct query {
    std::string trace_id;

    std::unique_ptr<RE2> line_pat;
    std::unique_ptr<RE2> file_pat;
    std::unique_ptr<RE2> tree_pat;
    std::unique_ptr<RE2> tags_pat;
    struct {
        std::unique_ptr<RE2> file_pat;
        std::unique_ptr<RE2> tree_pat;
        std::unique_ptr<RE2> tags_pat;
    } negate;
};

class code_searcher {
public:
    code_searcher();
    ~code_searcher();
    void dump_index(const string& path);
    void load_index(const string& path);

    const indexed_tree *open_tree(const string &name, json_object *meta, const string& version);
    void index_file(const indexed_tree *tree,
                    const string& path,
                    StringPiece contents);
    void finalize();

    void set_alloc(chunk_allocator *alloc);
    chunk_allocator *alloc() { return alloc_; }

    vector<indexed_tree> trees() const;
    string name() const {
        return name_;
    };
    void set_name(const string& name) {
        name_ = name;
    }

    vector<indexed_file*>::const_iterator begin_files() {
        return files_.begin();
    }
    vector<indexed_file*>::const_iterator end_files() {
        return files_.end();
    }

    class search_thread {
    public:
        search_thread(code_searcher *cs);
        ~search_thread();

        // function that will be called to record a match
        typedef std::function<void (const struct match_result*)> callback_func;
        // function that will be called to transform a match
        typedef std::function<bool (struct match_result*)> transform_func;

        /* file_pat may be NULL */
        void match(const query& q, const callback_func& cb, match_stats *stats)
        {
            match(q, cb, transform_func(), stats);
        }
        void match(const query& q,
                   const callback_func& cb,
                   const transform_func& func,
                   match_stats *stats);
    protected:
        struct job {
            atomic_int pending;
            searcher *search;
            thread_queue<chunk*> chunks;
        };

        const code_searcher *cs_;
        vector<std::thread> threads_;
        thread_queue<job*> queue_;

        static void search_one(search_thread *);
    private:
        search_thread(const search_thread&);
        void operator=(const search_thread&);
    };

protected:
    string name_;
    string_hash lines_;
    chunk_allocator *alloc_;
    bool finalized_;
    vector<indexed_tree*> trees_;
    vector<indexed_file*> files_;

    friend class search_thread;
    friend class searcher;
    friend class codesearch_index;
    friend class load_allocator;
    friend class tag_searcher;
};

// dump_load.cc
chunk_allocator *make_dump_allocator(code_searcher *search, const string& path);
// chunk_allocator.cc
chunk_allocator *make_mem_allocator();

void default_re2_options(RE2::Options&);

#endif /* CODESEARCH_H */
