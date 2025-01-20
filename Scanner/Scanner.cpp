#pragma onse

#include <set>
#include <iomanip>
#include "Scanner.h"

bool isSign(char ch) {
    return (ch == '!' || ch == '#' || ch == '$' || ch == '%' ||
            ch == '&' || ch == '\'' || ch == '(' || ch == ')' ||
            ch == '*' || ch == '+' || ch == ',' || ch == '-' ||
            ch == '.' || ch == '/' || ch == ':' || ch == '<' ||
            ch == '=' || ch == '>' || ch == '?' || ch == '@' ||
            ch == '[' || ch == ']' || ch == '^' || ch == '{' ||
            ch == '}' || ch == '\n' || ch == '\t' || ch == ' ');
}

bool containsLetter(const std::string &str) {
    for (char ch: str) {
        if (std::isalpha(static_cast<unsigned char>(ch))) {
            return true;
        }
    }
    return false;
}

bool isSeparator(char ch) {
    return (ch == ' ' || ch == '\'' ||
            ch == '*' || ch == '+' || ch == '/' || ch == '-' || ch == '=' ||
            ch == '.' || ch == ',' || ch == ';' || ch == ':' ||
            ch == '\n' || ch == '\t' ||
            ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}' || ch == '<' || ch == '>');
}

std::string toUpperCase(const std::string &str) {
    std::string result = str;
    for (char &ch: result) {
        ch = std::toupper(static_cast<unsigned char>(ch));
    }
    return result;
}

void Scanner::FillRulesFromCSVFile(const std::string &fileName) {
    std::ifstream file(fileName);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + fileName);
    }

    std::string line;
    // Чтение 2 первых строк (состояния)
    if (std::getline(file, line)) {
        std::string statesString, outSymbolsString = line;
        if (std::getline(file, line)) {
            statesString = line;
            std::stringstream ssOut(outSymbolsString), ssStates(statesString);
            std::string outSymbol, state;
            std::getline(ssOut, outSymbol, ';');
            std::getline(ssStates, state, ';');
            while (std::getline(ssStates, state, ';')) {
                std::getline(ssOut, outSymbol, ';');
                if (!state.empty() && state != "\"\"" && outSymbol != "\"\"") {
                    m_states.emplace_back(state, outSymbol, std::set<int>());
                    m_statesMap[state] = m_states.size() - 1;
                } else {
                    throw std::invalid_argument("Wrong states format");
                }
            }
        }
    }

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string inSymbol;
        if (std::getline(ss, inSymbol, ';')) {
            std::string transition;
            if (ss.peek() == '\'') {
                m_inSymbols.push_back(";");
                inSymbol = ";";
                std::getline(ss, transition, ';');
            } else {
                m_inSymbols.push_back(inSymbol);
            }
            int index = 0;
            while (std::getline(ss, transition, ';')) {
                if (transition != "\"\"" && !transition.empty()) {
                    m_transitions.emplace_back(index, m_statesMap[transition], inSymbol);
                    m_states[index].transitions.insert(m_transitions.size() - 1);
                }
                index++;
            }
        }
    }

    file.close();
}

Token Scanner::FindToken(std::ifstream &file) {
    enum class TokenType {
        IDENTIFIER,
        STRING,
        COMMENT,
        BLOCK_COMMENT,
        BAD,
        NONE
    };
    auto findTokenWithoutComment = [&](std::ifstream &file, bool &findToken) -> Token {
        findToken = true;
        TokenType tokenStatus = TokenType::NONE;
        char ch;
        std::string line;
        int lineCount = m_currLineCount;
        int columnCount = m_currColumnCount;

        bool canBeIdentifier = false;    // Начинается с букв или _
        MooreState currentState = m_states[0];

        while (file.get(ch)) {
            switch (tokenStatus) {
                case TokenType::IDENTIFIER: {
                    if (!isSeparator(ch)) {
                        line += ch;
                        m_currColumnCount++;
                        if ((!std::isalpha(ch) && ch != '_' && !isdigit(ch)) || line.size() > 256) {
                            tokenStatus = TokenType::BAD;
                        }
                    } else {
                        file.unget();
                        if (containsLetter(line)) {
                            return {"IDENTIFIER", lineCount, columnCount, line};
                        } else {
                            return {"BAD", lineCount, columnCount, line};
                        }
                    }
                    continue;
                }
                case TokenType::STRING: {
                    if (ch == '\n') {
                        m_currLineCount++;
                        m_currColumnCount = 1;
                        return {"BAD", lineCount, columnCount, line};
                    } else if (ch != '\'') {
                        line += ch;
                        m_currColumnCount++;
                    } else {
                        m_currColumnCount++;
                        line += ch;
                        return {"STRING", lineCount, columnCount, line};
                    }
                    continue;
                }
                case TokenType::COMMENT: {
                    if (ch != '\n') {
                        line += ch;
                        m_currColumnCount++;
                    } else {
                        m_currLineCount++;
                        m_currColumnCount = 1;
                        findToken = false;
                        return {};
                    }
                    continue;
                }
                case TokenType::BLOCK_COMMENT: {
                    if (ch != '}') {
                        line += ch;
                        if (ch == '\n') {
                            m_currLineCount++;
                            m_currColumnCount = 1;
                        } else {
                            m_currColumnCount++;
                        }
                    } else {
                        m_currColumnCount++;
                        findToken = false;
                        return {};
                    }
                    continue;
                }
                case TokenType::BAD: {
                    if (isSeparator(ch)) {
                        file.unget();
                        return {"BAD", lineCount, columnCount, line};
                    } else {
                        line += ch;
                        m_currColumnCount++;
                    }
                    continue;
                }
                case TokenType::NONE: {
                    if (line.empty()) {
                        // Начало токена
                        if (ch == ' ' || ch == '\t') {
                            m_currColumnCount++;
                            continue;
                        }
                        if (ch == '\n') {
                            m_currLineCount++;
                            m_currColumnCount = 1;
                            continue;
                        }
                        if (std::isalpha(ch) || ch == '_') {
                            canBeIdentifier = true;
                        }
                        columnCount = m_currColumnCount;
                        lineCount = m_currLineCount;
                    }
                    bool find = false;
                    // Поиск по автомату
                    for (auto transitionInd: currentState.transitions) {
                        if (toUpperCase(m_transitions[transitionInd].m_inSymbol) == toUpperCase(std::string(1, ch))) {
                            //Если после \d. хрень, то выходим из автомата
                            if (!line.empty() && std::isdigit(line[line.size() - 1]) && ch == '.' &&
                                !std::isdigit(file.peek())) {
                                break;
                            }
                            find = true;
                            line += ch;
                            currentState = m_states[m_transitions[transitionInd].m_to];
                            break;
                        }
                    }
                    if (isSign(ch)) {
                        canBeIdentifier = false;
                    }
                    // Перешли в следующее состояние
                    if (find) {
                        m_currColumnCount++;
                        continue;
                    }

                    // Не нашли переход по причине конца автомата или неподходящего символа

                    // Это конечное состояние (Число/комментарий(//)/кл.слово)
                    // Не заходим если не конечное или нет сепаратора
                    if (currentState.outSymbol == "F" && (
                            (!line.empty() && isSeparator(line[line.size() - 1]))
                            || isSeparator(ch)
                            || ((isdigit(ch) || ch == '_') && !canBeIdentifier))) {
                        if (line == "//") {
                            tokenStatus = TokenType::COMMENT;
                            line += ch;
                            continue;
                        }
                        auto it = m_typeMap.find(toUpperCase(line));
                        file.unget();

                        if (it != m_typeMap.end()) {
                            return {it->second, lineCount, columnCount, line};
                        } else {
                            // Число(Real если . или E)
                            if (line.find('.') != std::string::npos || line.find('E') != std::string::npos) {
                                return {"DOUBLE", lineCount, columnCount, line};
                            } else {
                                if (line.size() <= 16) {
                                    return {"INTEGER", lineCount, columnCount, line};
                                } else {
                                    return {"BAD", lineCount, columnCount, line};
                                }
                            }
                        }
                    } else {
                        // Если не конечное или не конец токена
                        if (isSeparator(ch) && !canBeIdentifier) {
                            if (ch == '{') {
                                m_currColumnCount++;
                                currentState = m_states[0];
                                tokenStatus = TokenType::BLOCK_COMMENT;
                                continue;
                            }
                            if (ch == '}') {
                                m_currColumnCount++;
                                currentState = m_states[0];
                                tokenStatus = TokenType::BAD;
                                line += ch;
                                continue;
                            }
                            if (ch == '\'') {
                                m_currColumnCount++;
                                currentState = m_states[0];
                                tokenStatus = TokenType::STRING;
                                line += ch;
                                continue;
                            }
                            file.unget();
                            return {"BAD", lineCount, columnCount, line};
                        }
                        m_currColumnCount++;
                        line += ch;
                        currentState = m_states[0];
                        if (canBeIdentifier) {
                            tokenStatus = TokenType::IDENTIFIER;
                            canBeIdentifier = false;
                            continue;
                        } else {
                            // Ошибка(Не кл.слово, не сепаратор и не идентификатор)
                            tokenStatus = TokenType::BAD;
                            continue;
                        }
                    }
                }
            }
        }

        // Обработка последнего токена
        if (currentState.outSymbol == "F" && tokenStatus != TokenType::COMMENT) {
            if (line == "//") {
                findToken = false;
                return {};
            }
            auto it = m_typeMap.find(toUpperCase(line));
            if (it != m_typeMap.end()) {
                return {it->second, lineCount, columnCount, line};
            } else {
                // Число(Real если . или E)
                if (line.find('.') != std::string::npos || line.find('E') != std::string::npos) {
                    return {"DOUBLE", lineCount, columnCount, line};
                } else {
                    if (line.size() <= 16) {
                        return {"INTEGER", lineCount, columnCount, line};
                    } else {
                        return {"BAD", lineCount, columnCount, line};
                    }
                }
            }
        } else {
            if (!line.empty()) {
                if (tokenStatus == TokenType::COMMENT) {
                    findToken = false;
                    return {};
                }
                if (tokenStatus == TokenType::STRING || tokenStatus == TokenType::BLOCK_COMMENT ||
                    tokenStatus == TokenType::BAD) {
                    return {"BAD", lineCount, columnCount, line};
                }
                if (canBeIdentifier || tokenStatus == TokenType::IDENTIFIER) {
                    if (containsLetter(line)) {
                        return {"IDENTIFIER", lineCount, columnCount, line};
                    } else {
                        return {"BAD", lineCount, columnCount, line};
                    }
                }
            }
            if (tokenStatus == TokenType::BLOCK_COMMENT) {
                return {"BAD", lineCount, columnCount, line};
            }
        }
        return {"EOF", lineCount, columnCount, ""};
    };

    bool findToken = false;
    while (!findToken) {
        Token token = findTokenWithoutComment(file, findToken);
        if (findToken) {
            return token;
        }
    }
}

void Scanner::ScanFile(const std::string &fileName, const std::string &outFilename) {
    std::ifstream file(fileName);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + fileName);
    }

    std::ofstream outFile(outFilename);
    if (!outFile.is_open()) {
        throw std::runtime_error("Could not open file: " + outFilename);
    }

    while (true) {
        const auto token = FindToken(file);
        if (token.m_type == "EOF") {
            break;
        }
        if (token.m_type == "IF")
            std::cout << std::endl;
//        AddToStatistic(token);
        WriteTokenToFile(outFile, token);
    }
    for (auto &st: m_statistic) {
        std::cout << std::setw(15) << st.second.first << ": " << st.second.second << " - " << st.first << std::endl;
    }
    file.close();
}

void Scanner::WriteTokenToFile(std::ofstream &file, const Token &token) {
    file << token.m_type << " (" << token.m_line << ", " << token.m_column << ") \"" << token.m_value << "\""
         << std::endl;
}

void Scanner::AddToStatistic(const Token &token) {
    if (token.m_type != "IDENTIFIER") {
        return;
    }
    auto it = m_statistic.find(token.m_value);
    if (it != m_statistic.end()) {
        it->second.second++;
    } else {
        m_statistic[token.m_value] = {token.m_type, 1};
    }
}

