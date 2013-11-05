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
            "  --in|-i [match]       Keep hunks that match this pattern\n"
            "  --out|-o|-d [match]   Filter out hunks match this pattern\n");
}

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

    char *mPattern;
};

class RegexpMatch : public Match
{
public:
    RegexpMatch(Type type, char *pattern)
        : Match(type)
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
    regex_t mRegex;
};

static inline void processHunk(const std::vector<std::pair<std::string, bool> > &lines, const std::vector<Match*> &matches)
{
    size_t match = matches.size();
    for (std::vector<std::pair<std::string, bool> >::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        if (it->second) {
            for (size_t m=0; m<match; ++m) {
                if (matches.at(m)->match(it->first.c_str())) {
                    match = m;
                    break;
                }
            }
        }
    }
    if (match == matches.size() || matches.at(match)->type == Match::In) {
        for (std::vector<std::pair<std::string, bool> >::const_iterator it = lines.begin(); it != lines.end(); ++it) {
            fwrite(it->first.c_str(), it->first.size(), 1, stdout);
        }
    }
}

static void processFile(FILE *f, const std::vector<Match*> &matches)
{
    assert(f);
    char buf[16384];
    std::vector<std::pair<std::string, bool> > pending;
    while (fgets(buf, sizeof(buf), f)) {
        if (!strncmp("--- ", buf, 4) || isdigit(buf[0])) {
            processHunk(pending, matches);
            pending.clear();
            pending.push_back(std::make_pair(buf, false));
        } else if (strncmp("+++ ", buf, 4) && strncmp("@@ ", buf, 3)) {
            switch (buf[0]) {
            case '+':
            case '>':
            case '-':
            case '<':
                pending.push_back(std::make_pair(buf, true));
                break;
            case ' ':
                pending.push_back(std::make_pair(buf, false));
                break;
            default:
                fprintf(stderr, "Unexpected line: [%s]\n", buf);
                break;
            }
        } else {
            pending.push_back(std::make_pair(buf, false));
        }
    }
    processHunk(pending, matches);
}

int main(int argc, char **argv)
{
    struct option opts[] = {
        { "help", no_argument, 0, 'h' },
        { "match-raw", no_argument, 0, 'r' },
        { "in", required_argument, 0, 'i' },
        { "out", required_argument, 0, 'o' },
        { 0, 0, 0, 0 }
    };
    bool raw = false;
    std::vector<std::pair<char*, bool> > input;
    std::vector<Match*> matches;
    while (true) {
        const int c = getopt_long(argc, argv, "hri:o:d:", opts, 0);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'h':
            usage(stdout);
            return 0;
        case 'r':
            raw = true;
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
        matches.push_back(raw
                          ? static_cast<Match*>(new RawMatch(type, it->first)) :
                          static_cast<Match*>(new RegexpMatch(type, it->first)));
    }

    if (optind == argc) {
        processFile(stdin, matches);
    } else {
        while (optind < argc) {
            FILE *f = fopen(argv[optind++], "r");
            if (!f) {
                fprintf(stderr, "Can't open %s for reading\n", argv[optind - 1]);
                return 2;
            }
            processFile(f, matches);
            fclose(f);
        }
    }
    for (std::vector<Match*>::const_iterator it = matches.begin(); it != matches.end(); ++it) {
        delete *it;
    }
    return 0;
}
