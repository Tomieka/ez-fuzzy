#include "syntaxhighlighter.h"
#include <QDebug>
#include <QFont>

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    HighlightingRule rule;

    // Keyword format (bold blue)
    keywordFormat.setForeground(Qt::blue);
    keywordFormat.setFontWeight(QFont::Bold);
    QStringList keywordPatterns;
    keywordPatterns << "\\bchar\\b" << "\\bclass\\b" << "\\bconst\\b"
                    << "\\bdouble\\b" << "\\benum\\b" << "\\bexplicit\\b" << "\\bfriend\\b"
                    << "\\binline\\b" << "\\bint\\b" << "\\blong\\b" << "\\bnamespace\\b"
                    << "\\boperator\\b" << "\\bprivate\\b" << "\\bprotected\\b" << "\\bpublic\\b"
                    << "\\bshort\\b" << "\\bsignals\\b" << "\\bsigned\\b" << "\\bslots\\b"
                    << "\\bstatic\\b" << "\\bstruct\\b" << "\\btemplate\\b" << "\\btypedef\\b"
                    << "\\btypename\\b" << "\\bunion\\b" << "\\bunsigned\\b" << "\\bvirtual\\b"
                    << "\\bvoid\\b" << "\\bvolatile\\b" << "\\bbool\\b" << "\\btrue\\b" << "\\bfalse\\b"
                    << "\\bif\\b" << "\\belse\\b" << "\\bfor\\b" << "\\bwhile\\b" << "\\bswitch\\b"
                    << "\\bcase\\b" << "\\bbreak\\b" << "\\bcontinue\\b" << "\\bdefault\\b"
                    << "\\breturn\\b" << "\\bdelete\\b" << "\\bnew\\b" << "\\bthrow\\b" << "\\btry\\b" << "\\bcatch\\b";
    foreach (const QString &pattern, keywordPatterns) {
        rule.pattern = QRegularExpression(pattern);
        rule.format = keywordFormat;
        highlightingRules.append(rule);
    }

    // Class format (bold magenta)
    classFormat.setFontWeight(QFont::Bold);
    classFormat.setForeground(Qt::darkMagenta);
    rule.pattern = QRegularExpression("\\bQ[A-Za-z]+\\b");
    rule.format = classFormat;
    highlightingRules.append(rule);

    // Single line comment format (italic green)
    singleLineCommentFormat.setForeground(Qt::darkGreen);
    singleLineCommentFormat.setFontItalic(true);
    rule.pattern = QRegularExpression("//[^\\n]*"); // Fixed escape
    rule.format = singleLineCommentFormat;
    highlightingRules.append(rule);

    // Multi-line comment format (green)
    multiLineCommentFormat.setForeground(Qt::darkGreen);
    commentStartExpression = QRegularExpression("/\\*");
    commentEndExpression = QRegularExpression("\\*/");

    // Quotation format (red)
    quotationFormat.setForeground(Qt::red);
    rule.pattern = QRegularExpression("\".*\"");
    rule.format = quotationFormat;
    highlightingRules.append(rule);

    // Function format (italic blue)
    functionFormat.setForeground(Qt::blue);
    functionFormat.setFontItalic(true);
    rule.pattern = QRegularExpression("\\b[A-Za-z0-9_]+(?=\\()");
    rule.format = functionFormat;
    highlightingRules.append(rule);

    // Preprocessor format (dark cyan)
    preprocessorFormat.setForeground(Qt::darkCyan);
    preprocessorFormat.setFontWeight(QFont::Bold);
    rule.pattern = QRegularExpression("^#[^\\n]*"); // Fixed escape
    rule.format = preprocessorFormat;
    highlightingRules.append(rule);
}

void SyntaxHighlighter::highlightBlock(const QString &text)
{
    foreach (const HighlightingRule &rule, highlightingRules) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
    setCurrentBlockState(0);

    int startIndex = 0;
    if (previousBlockState() != 1)
        startIndex = text.indexOf(commentStartExpression);

    while (startIndex >= 0) {
        QRegularExpressionMatch match = commentEndExpression.match(text, startIndex);
        int endIndex = match.capturedStart();
        int commentLength = 0;
        if (endIndex == -1) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex + match.capturedLength();
        }
        setFormat(startIndex, commentLength, multiLineCommentFormat);
        startIndex = text.indexOf(commentStartExpression, startIndex + commentLength);
    }
} 