#include <bits/stdc++.h>
using namespace std;

enum class JudgeStatus {
    Accepted,
    Wrong_Answer,
    Runtime_Error,
    Time_Limit_Exceed
};

static inline bool toJudgeStatus(const string &s, JudgeStatus &out) {
    if (s == "Accepted") { out = JudgeStatus::Accepted; return true; }
    if (s == "Wrong_Answer") { out = JudgeStatus::Wrong_Answer; return true; }
    if (s == "Runtime_Error") { out = JudgeStatus::Runtime_Error; return true; }
    if (s == "Time_Limit_Exceed") { out = JudgeStatus::Time_Limit_Exceed; return true; }
    return false;
}

struct SubmissionRec {
    char problem; // 'A'..'Z'
    string statusStr;
    int time; // submission time
};

struct ProblemState {
    // Core solution status
    bool solved = false;
    int firstAcceptedTime = 0; // valid only if solved
    int wrongBeforeFirstAccepted = 0; // total wrong before the first Accepted over entire timeline

    // Freeze related snapshots
    int wrongBeforeFreezeAtFreeze = 0; // snapshot at FREEZE
    bool wasUnsolvedAtFreeze = false;
    int postFreezeSubmissionCount = 0; // y in -x/y
    bool hasPostFreezeActivity = false; // y>0
    bool unfrozenInScroll = false; // during SCROLL, whether this problem has been unfrozen
};

struct Team {
    string name;
    vector<ProblemState> problems; // size M
    vector<SubmissionRec> submissions; // for QUERY_SUBMISSION
};

struct Contest {
    bool started = false;
    int duration = 0;
    int problemCount = 0; // M

    bool isFrozen = false; // global freeze flag

    // teams and mapping
    vector<Team> teams;
    unordered_map<string,int> nameToIndex;

    // last flushed ranking (indices into teams)
    vector<int> lastFlushedOrder; // order of team indices

    // Helper to (re)initialize team problem vectors when START happens
    void initializeTeamsProblems(int m) {
        for (auto &t : teams) {
            t.problems.assign(m, ProblemState{});
        }
        // initial lastFlushedOrder: lexicographic by team name
        lastFlushedOrder.resize(teams.size());
        iota(lastFlushedOrder.begin(), lastFlushedOrder.end(), 0);
        stable_sort(lastFlushedOrder.begin(), lastFlushedOrder.end(), [&](int a, int b){
            return teams[a].name < teams[b].name;
        });
    }

    bool addTeam(const string &name) {
        if (started) return false;
        if (nameToIndex.find(name) != nameToIndex.end()) return false;
        Team t; t.name = name; // problems vector will be sized on START
        int idx = (int)teams.size();
        teams.push_back(move(t));
        nameToIndex[name] = idx;
        return true;
    }

    bool startContest(int dur, int m) {
        if (started) return false;
        started = true;
        duration = dur;
        problemCount = m;
        initializeTeamsProblems(m);
        return true;
    }

    void snapshotFreeze() {
        // Called when entering FREEZE
        for (auto &t : teams) {
            for (int p = 0; p < problemCount; ++p) {
                auto &ps = t.problems[p];
                ps.wrongBeforeFreezeAtFreeze = ps.wrongBeforeFirstAccepted;
                ps.wasUnsolvedAtFreeze = !ps.solved;
                ps.postFreezeSubmissionCount = 0;
                ps.hasPostFreezeActivity = false;
                ps.unfrozenInScroll = false;
            }
        }
    }

    void clearFreezeFlags() {
        for (auto &t : teams) {
            for (int p = 0; p < problemCount; ++p) {
                auto &ps = t.problems[p];
                ps.wasUnsolvedAtFreeze = false;
                ps.postFreezeSubmissionCount = 0;
                ps.hasPostFreezeActivity = false;
                ps.unfrozenInScroll = false;
                ps.wrongBeforeFreezeAtFreeze = 0;
            }
        }
    }

    void recordSubmit(char problemChar, const string &teamName, const string &statusStr, int time) {
        int ti = nameToIndex[teamName];
        Team &t = teams[ti];
        int p = problemChar - 'A';
        ProblemState &ps = t.problems[p];

        // Log submission (for QUERY_SUBMISSION)
        t.submissions.push_back(SubmissionRec{problemChar, statusStr, time});

        // Freeze accounting: count post-freeze submissions on problems unsolved at FREEZE
        if (isFrozen && ps.wasUnsolvedAtFreeze && !ps.unfrozenInScroll) {
            ps.postFreezeSubmissionCount++;
            ps.hasPostFreezeActivity = true;
        }

        // Update core problem status
        JudgeStatus st;
        bool ok = toJudgeStatus(statusStr, st);
        (void)ok; // input guaranteed valid
        if (!ps.solved) {
            if (st == JudgeStatus::Accepted) {
                ps.solved = true;
                ps.firstAcceptedTime = time;
            } else {
                // wrong attempts before first accept
                ps.wrongBeforeFirstAccepted++;
            }
        } else {
            // already solved: submissions after first AC do not change counts for scoreboard
        }
    }

    struct RankKey {
        int solved = 0;
        long long penalty = 0;
        vector<int> solveTimesDesc; // sorted descending
    };

    RankKey buildRankKeyForTeamVisible(int ti) const {
        const Team &t = teams[ti];
        RankKey key;
        key.solved = 0;
        key.penalty = 0;
        key.solveTimesDesc.clear();
        key.solveTimesDesc.reserve(problemCount);
        for (int p = 0; p < problemCount; ++p) {
            const ProblemState &ps = t.problems[p];
            bool frozenThis = isFrozen && ps.wasUnsolvedAtFreeze && ps.hasPostFreezeActivity && !ps.unfrozenInScroll;
            if (frozenThis) {
                continue; // invisible while frozen
            }
            if (ps.solved) {
                key.solved += 1;
                key.penalty += 20LL * ps.wrongBeforeFirstAccepted + ps.firstAcceptedTime;
                key.solveTimesDesc.push_back(ps.firstAcceptedTime);
            }
        }
        sort(key.solveTimesDesc.begin(), key.solveTimesDesc.end(), greater<int>());
        return key;
    }

    bool teamLess(int a, int b) const {
        // return true if team a ranks higher than team b
        RankKey ka = buildRankKeyForTeamVisible(a);
        RankKey kb = buildRankKeyForTeamVisible(b);
        if (ka.solved != kb.solved) return ka.solved > kb.solved;
        if (ka.penalty != kb.penalty) return ka.penalty < kb.penalty;
        // compare solve time vectors: smaller values are better at earlier positions, but vectors are desc sorted
        size_t n = ka.solveTimesDesc.size(); // equals kb's size
        for (size_t i = 0; i < n; ++i) {
            if (ka.solveTimesDesc[i] != kb.solveTimesDesc[i]) {
                return ka.solveTimesDesc[i] < kb.solveTimesDesc[i];
            }
        }
        // finally by team name lex ascending
        return teams[a].name < teams[b].name;
    }

    vector<int> computeCurrentOrder() const {
        vector<int> order(teams.size());
        iota(order.begin(), order.end(), 0);
        stable_sort(order.begin(), order.end(), [&](int a, int b){ return teamLess(a,b); });
        return order;
    }

    RankKey getRankKeyCachedless(int ti) const { return buildRankKeyForTeamVisible(ti); }

    void flushScoreboard() {
        lastFlushedOrder = computeCurrentOrder();
    }

    string problemCellFor(const ProblemState &ps) const {
        bool frozenThis = isFrozen && ps.wasUnsolvedAtFreeze && ps.hasPostFreezeActivity && !ps.unfrozenInScroll;
        if (frozenThis) {
            int x = ps.wrongBeforeFreezeAtFreeze;
            int y = ps.postFreezeSubmissionCount;
            if (x == 0) {
                return to_string(0) + "/" + to_string(y);
            } else {
                return string("-") + to_string(x) + "/" + to_string(y);
            }
        }
        if (ps.solved) {
            if (ps.wrongBeforeFirstAccepted == 0) return string("+");
            return string("+") + to_string(ps.wrongBeforeFirstAccepted);
        }
        if (ps.wrongBeforeFirstAccepted == 0) return string(".");
        return string("-") + to_string(ps.wrongBeforeFirstAccepted);
    }

    vector<string> buildScoreboardLines(const vector<int> &order) const {
        vector<string> lines;
        lines.reserve(order.size());
        // Precompute rank keys for efficiency (still small)
        vector<RankKey> keys(teams.size());
        for (size_t i = 0; i < order.size(); ++i) {
            keys[order[i]] = buildRankKeyForTeamVisible(order[i]);
        }
        for (size_t i = 0; i < order.size(); ++i) {
            int ti = order[i];
            const Team &t = teams[ti];
            const RankKey &k = keys[ti];
            // team_name ranking solved_count total_penalty A B C ...
            string line;
            line.reserve(64);
            line += t.name;
            line += ' ';
            line += to_string((int)i + 1);
            line += ' ';
            line += to_string(k.solved);
            line += ' ';
            line += to_string(k.penalty);
            for (int p = 0; p < problemCount; ++p) {
                line += ' ';
                line += problemCellFor(t.problems[p]);
            }
            lines.push_back(move(line));
        }
        return lines;
    }

    void printScoreboard(const vector<int> &order) const {
        auto lines = buildScoreboardLines(order);
        for (const auto &ln : lines) {
            cout << ln << '\n';
        }
    }

    // Find lowest-ranked team with at least one frozen problem, given current order
    int findLowestWithFrozen(const vector<int> &order) const {
        for (int i = (int)order.size() - 1; i >= 0; --i) {
            int ti = order[i];
            if (teamHasFrozen(ti)) return i;
        }
        return -1;
    }

    bool teamHasFrozen(int ti) const {
        const Team &t = teams[ti];
        for (int p = 0; p < problemCount; ++p) {
            const ProblemState &ps = t.problems[p];
            bool frozenThis = isFrozen && ps.wasUnsolvedAtFreeze && ps.hasPostFreezeActivity && !ps.unfrozenInScroll;
            if (frozenThis) return true;
        }
        return false;
    }

    int smallestFrozenProblemIndex(int ti) const {
        const Team &t = teams[ti];
        for (int p = 0; p < problemCount; ++p) {
            const ProblemState &ps = t.problems[p];
            bool frozenThis = isFrozen && ps.wasUnsolvedAtFreeze && ps.hasPostFreezeActivity && !ps.unfrozenInScroll;
            if (frozenThis) return p;
        }
        return -1;
    }

    // Perform SCROLL process
    void doScroll() {
        // Precondition: isFrozen == true checked by caller
        cout << "[Info]Scroll scoreboard.\n";
        // Scroll requires a flush first (without printing flush info)
        vector<int> order = computeCurrentOrder();
        // Print scoreboard before scrolling
        printScoreboard(order);

        // Loop until no team has frozen problems
        while (true) {
            int pos = findLowestWithFrozen(order);
            if (pos < 0) break;
            int ti = order[pos];
            int pi = smallestFrozenProblemIndex(ti);
            if (pi < 0) break; // safety

            // Unfreeze this problem
            teams[ti].problems[pi].unfrozenInScroll = true;

            // Recompute order after this unfreeze
            vector<int> newOrder = computeCurrentOrder();

            // If ranking of this team improved, output ranking change
            int oldIdx = -1, newIdx = -1;
            for (int i = 0; i < (int)order.size(); ++i) if (order[i] == ti) { oldIdx = i; break; }
            for (int i = 0; i < (int)newOrder.size(); ++i) if (newOrder[i] == ti) { newIdx = i; break; }
            if (oldIdx >= 0 && newIdx >= 0 && newIdx < oldIdx) {
                // Find the team that was previously at newIdx in the old order
                string replacedTeamName = teams[ order[newIdx] ].name;
                RankKey rk = buildRankKeyForTeamVisible(ti);
                cout << teams[ti].name << ' ' << replacedTeamName << ' ' << rk.solved << ' ' << rk.penalty << "\n";
            }

            order.swap(newOrder);
        }

        // After scrolling ends, print the final scoreboard
        printScoreboard(order);

        // Lift frozen state
        isFrozen = false;
        clearFreezeFlags();
        // After scroll, the latest state is effectively flushed; we can update lastFlushedOrder
        lastFlushedOrder = computeCurrentOrder();
    }
};

static vector<string> split(const string &s) {
    vector<string> tokens; tokens.reserve(16);
    string cur; cur.reserve(16);
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (isspace((unsigned char)c)) {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Contest contest;

    string line;
    while (std::getline(cin, line)) {
        if (line.empty()) continue;
        auto tokens = split(line);
        if (tokens.empty()) continue;
        const string &cmd = tokens[0];

        if (cmd == "ADDTEAM") {
            const string &name = tokens[1];
            if (!contest.addTeam(name)) {
                if (contest.started) {
                    cout << "[Error]Add failed: competition has started.\n";
                } else {
                    cout << "[Error]Add failed: duplicated team name.\n";
                }
            } else {
                cout << "[Info]Add successfully.\n";
            }
        } else if (cmd == "START") {
            // START DURATION [duration_time] PROBLEM [problem_count]
            // tokens: START DURATION x PROBLEM y
            if (contest.started) {
                cout << "[Error]Start failed: competition has started.\n";
            } else {
                int dur = stoi(tokens[2]);
                int m = stoi(tokens[4]);
                contest.startContest(dur, m);
                cout << "[Info]Competition starts.\n";
            }
        } else if (cmd == "SUBMIT") {
            // SUBMIT [problem_name] BY [team_name] WITH [submit_status] AT [time]
            char prob = tokens[1][0];
            string teamName = tokens[3];
            string statusStr = tokens[5];
            int time = stoi(tokens[7]);
            contest.recordSubmit(prob, teamName, statusStr, time);
        } else if (cmd == "FLUSH") {
            contest.flushScoreboard();
            cout << "[Info]Flush scoreboard.\n";
        } else if (cmd == "FREEZE") {
            if (contest.isFrozen) {
                cout << "[Error]Freeze failed: scoreboard has been frozen.\n";
            } else {
                contest.isFrozen = true;
                contest.snapshotFreeze();
                cout << "[Info]Freeze scoreboard.\n";
            }
        } else if (cmd == "SCROLL") {
            if (!contest.isFrozen) {
                cout << "[Error]Scroll failed: scoreboard has not been frozen.\n";
            } else {
                contest.doScroll();
            }
        } else if (cmd == "QUERY_RANKING") {
            string teamName = tokens[1];
            auto it = contest.nameToIndex.find(teamName);
            if (it == contest.nameToIndex.end()) {
                cout << "[Error]Query ranking failed: cannot find the team.\n";
            } else {
                cout << "[Info]Complete query ranking.\n";
                if (contest.isFrozen) {
                    cout << "[Warning]Scoreboard is frozen. The ranking may be inaccurate until it were scrolled.\n";
                }
                // Build a map from team index to rank based on lastFlushedOrder
                int rank = -1;
                for (size_t i = 0; i < contest.lastFlushedOrder.size(); ++i) {
                    if (contest.lastFlushedOrder[i] == it->second) { rank = (int)i + 1; break; }
                }
                // If never flushed yet, lastFlushedOrder may not include newly added teams after START (but spec says operations after START)
                if (rank < 0) {
                    // fallback: recompute and build order by lex name as initial state if needed
                    vector<int> order = contest.lastFlushedOrder.empty() ? vector<int>() : contest.lastFlushedOrder;
                    if (order.empty()) {
                        order.resize(contest.teams.size());
                        iota(order.begin(), order.end(), 0);
                        stable_sort(order.begin(), order.end(), [&](int a, int b){ return contest.teams[a].name < contest.teams[b].name; });
                    }
                    for (size_t i = 0; i < order.size(); ++i) if (order[i] == it->second) { rank = (int)i + 1; break; }
                    if (rank < 0) rank = 1; // minimal fallback
                }
                cout << teamName << " NOW AT RANKING " << rank << "\n";
            }
        } else if (cmd == "QUERY_SUBMISSION") {
            string teamName = tokens[1];
            auto it = contest.nameToIndex.find(teamName);
            if (it == contest.nameToIndex.end()) {
                cout << "[Error]Query submission failed: cannot find the team.\n";
            } else {
                // tokens: QUERY_SUBMISSION team WHERE PROBLEM=xxx AND STATUS=yyy
                // parse PROBLEM
                string probSpec = tokens[3]; // PROBLEM=...
                string statusSpec = tokens[5]; // STATUS=...
                string probVal = probSpec.substr(strlen("PROBLEM="));
                string statusVal = statusSpec.substr(strlen("STATUS="));
                cout << "[Info]Complete query submission.\n";

                const Team &t = contest.teams[it->second];
                bool found = false;
                SubmissionRec last{};
                for (int i = (int)t.submissions.size() - 1; i >= 0; --i) {
                    const SubmissionRec &sr = t.submissions[i];
                    bool probOk = (probVal == "ALL") || (sr.problem == probVal[0]);
                    bool statusOk = (statusVal == "ALL") || (sr.statusStr == statusVal);
                    if (probOk && statusOk) {
                        last = sr; found = true; break;
                    }
                }
                if (!found) {
                    cout << "Cannot find any submission.\n";
                } else {
                    cout << t.name << ' ' << last.problem << ' ' << last.statusStr << ' ' << last.time << "\n";
                }
            }
        } else if (cmd == "END") {
            cout << "[Info]Competition ends.\n";
            break;
        } else {
            // Unknown command: per spec, inputs are valid; ignore
        }
    }
    return 0;
}
