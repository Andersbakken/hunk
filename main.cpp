#include <getopt.h>
#include <string>
#include <vector>
#include <assert.h>
#include <regex.h>

static void usage(FILE *f)
{
    fprintf(f,
            "hunk [options...]\n"
            "  --help|-h             Display this help\n"
            "  --match-raw|-r        Don't treat patterns as regexps\n"
            "  --match-context|-c    Apply matches to context lines\n"
            "  --match-headers|-H    Apply matches to header lines\n"
            "  --verbose|-v          Be verbose\n"
            "  --in|-i [match]       Keep hunks that match this pattern\n"
            "  --out|-o|-d [match]   Filter out hunks match this pattern\n");
}

enum Flag {
    MatchContext = 0x1,
    MatchHeaders = 0x2,
    Raw = 0x4,
    Verbose = 0x8
};

class Match
{
public:
    enum Type {
        In,
        Out
    };

    Match(Type t)
        : type(t)
    {}

    virtual ~Match()
    {}

    virtual bool match(const char *line) const = 0;
    virtual std::string toString() const = 0;

    const Type type;
};

class RawMatch : public Match
{
public:
    RawMatch(Type type, char *pattern)
        : Match(type), mPattern(pattern)
    {}

    virtual bool match(const char *line) const
    {
        return strstr(mPattern, line);
    }

    virtual std::string toString() const
    {
        char buf[1024];
        snprintf(buf, sizeof(buf), "--%s=%s", type == In ? "in" : "out", mPattern);
        return buf;
    }

    char *mPattern;
};

class RegexpMatch : public Match
{
public:
    RegexpMatch(Type type, char *pattern)
        : Match(type), mPattern(pattern)
    {
        if (regcomp(&mRegex, pattern, 0)) {
            fprintf(stderr, "Invalid regexp %s\n", pattern);
            exit(3);
        }
    }

    ~RegexpMatch()
    {
        regfree(&mRegex);
    }

    virtual bool match(const char *line) const
    {
        return !regexec(&mRegex, line, 0, 0, 0);
    }

    virtual std::string toString() const
    {
        char buf[1024];
        snprintf(buf, sizeof(buf), "--%s=%s", type == In ? "in" : "out", mPattern);
        return buf;
    }
    
    regex_t mRegex;
    char *mPattern;
};

static inline void processHunk(const std::vector<std::pair<std::string, bool> > &lines,
                               const std::vector<Match*> &matches,
                               unsigned int flags)
{
    size_t match = matches.size();
    bool hasIns = false;
    if (flags & Verbose) {
        fprintf(stderr, "Parsing hunk\n");
        for (std::vector<std::pair<std::string, bool> >::const_iterator it = lines.begin(); it != lines.end(); ++it) {
            fprintf(stderr, "%s %s", it->second ? "t" : "nil", it->first.c_str());
        }
    }
    for (std::vector<std::pair<std::string, bool> >::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        if (it->second) {
            for (size_t m=0; m<match; ++m) {
                if (matches.at(m)->type == Match::In)
                    hasIns = true;
                if (matches.at(m)->match(it->first.c_str())) {
                    if (flags & Verbose)
                        fprintf(stderr, "Matched %s %s", matches.at(m)->toString().c_str(), it->first.c_str());
                    match = m;
                    break;
                }
            }
        }
    }
    if (hasIns && match == matches.size()) {
        if (flags & Verbose)
            fprintf(stderr, "Hunk was discarded because of no matches\n");
        return;
    } else if (match < matches.size() && matches.at(match)->type == Match::Out) {
        if (flags & Verbose)
            fprintf(stderr, "Hunk was discarded because of match %zu\n", match);
        return;
    }
    if (flags & Verbose)
        fprintf(stderr, "Hunk matched. printing %zu lines\n", lines.size());

    for (std::vector<std::pair<std::string, bool> >::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        fwrite(it->first.c_str(), it->first.size(), 1, stdout);
    }
}

static void processFile(FILE *f, const std::vector<Match*> &matches, unsigned int flags)
{
    assert(f);
    char buf[16384];
    std::vector<std::pair<std::string, bool> > pending;
    bool seenHunkStart = false;
    while (fgets(buf, sizeof(buf), f)) {
        if (!strncmp("--- ", buf, 4) || isdigit(buf[0])) {
            if (seenHunkStart) {
                processHunk(pending, matches, flags);
                pending.clear();
            }
            seenHunkStart = true;
            pending.push_back(std::make_pair(buf, flags & MatchHeaders));
        } else if (strncmp("+++ ", buf, 4) && strncmp("@@ ", buf, 3)) {
            switch (buf[0]) {
            case '+':
            case '>':
            case '-':
            case '<':
                pending.push_back(std::make_pair(buf, true));
                break;
            case ' ':
                pending.push_back(std::make_pair(buf, flags & MatchContext));
                break;
            default:
                if (seenHunkStart) {
                    processHunk(pending, matches, flags);
                    pending.clear();
                    seenHunkStart = false;
                }

                pending.push_back(std::make_pair(buf, flags & MatchHeaders));
                break;
            }
        } else {
            pending.push_back(std::make_pair(buf, flags & MatchHeaders));
        }
    }
    processHunk(pending, matches, flags);
}

int main(int argc, char **argv)
{
    struct option opts[] = {
        { "help", no_argument, 0, 'h' },
        { "match-raw", no_argument, 0, 'r' },
        { "match-context", no_argument, 0, 'c' },
        { "match-headers", no_argument, 0, 'H' },
        { "in", required_argument, 0, 'i' },
        { "out", required_argument, 0, 'o' },
        { "verbose", no_argument, 0, 'v' },
        { 0, 0, 0, 0 }
    };
    unsigned int flags = 0;
    std::vector<std::pair<char*, bool> > input;
    std::vector<Match*> matches;
    while (true) {
        const int c = getopt_long(argc, argv, "hri:o:d:cHv", opts, 0);
        if (c == -1)
            break;

        switch (c) {
        case 'h':
            usage(stdout);
            return 0;
        case 'v':
            flags |= Verbose;
            break;
        case 'r':
            flags |= Raw;
            break;
        case 'c':
            flags |= MatchContext;
            break;
        case 'H':
            flags |= MatchHeaders;
            break;
        case 'i':
            input.push_back(std::make_pair(optarg, true));
            break;
        case 'o':
        case 'd':
            input.push_back(std::make_pair(optarg, false));
            break;
        default:
            usage(stderr);
            return 1;
        }
    }
    if (input.empty()) {
        fprintf(stderr, "No matches\n");
        return 4;
    }

    for (std::vector<std::pair<char*, bool> >::const_iterator it = input.begin(); it != input.end(); ++it) {
        const Match::Type type = it->second ? Match::In : Match::Out;
        matches.push_back(flags & Raw
                          ? static_cast<Match*>(new RawMatch(type, it->first)) :
                          static_cast<Match*>(new RegexpMatch(type, it->first)));
    }

    if (optind == argc) {
        processFile(stdin, matches, flags);
    } else {
        while (optind < argc) {
            FILE *f = fopen(argv[optind++], "r");
            if (!f) {
                fprintf(stderr, "Can't open %s for reading\n", argv[optind - 1]);
                return 2;
            }
            processFile(f, matches, flags);
            fclose(f);
        }
    }
    for (std::vector<Match*>::const_iterator it = matches.begin(); it != matches.end(); ++it) {
        delete *it;
    }
    return 0;
}
