#include <QCoreApplication>



#include <QDebug>
#include <QFile>
#include <QTextStream>



#include <errno.h>
#include <zlib.h>

#include "minisat/utils/System.h"
#include "minisat/utils/ParseUtils.h"
#include "minisat/utils/Options.h"
#include "minisat/core/Dimacs.h"
#include "minisat/simp/SimpSolver.h"



using namespace std;
using namespace Minisat;

//=================================================================================================


static Solver* solver;
// Terminate by notifying the solver and back out gracefully. This is mainly to have a test-case
// for this feature of the Solver as it may take longer than an immediate call to '_exit()'.
static void SIGINT_interrupt(int) { solver->interrupt(); }

// Note that '_exit()' rather than 'exit()' has to be used. The reason is that 'exit()' calls
// destructors and may cause deadlocks if a malloc/free function happens to be running (these
// functions are guarded by locks for multithreaded use).
static void SIGINT_exit(int) {
    printf("\n"); printf("*** INTERRUPTED ***\n");
    if (solver->verbosity > 0){
        solver->printStats();
        printf("\n"); printf("*** INTERRUPTED ***\n"); }
    _exit(1); }


//=================================================================================================



QString ruleFileName = "rule.txt";
QString cnfFileName = "clause.cnf";
int BIT_LENGTH = 8;
QMap<int,QString> mapVariables;
int globalIndex = 1;


QString convertToBinary(int integer);
QString generateSumRule(QString __x, QString __y, QString __z)
{

    qDebug() << __x << "+" << __y << "=" << __z;

//    QString carry = QString("c_").append("%1+%2").arg(__x).arg(__y);
    QString carry = QString("carry").append("%1").arg(__z);

    QString result = "";

    QString sum0 = QString("!%1_0 || %2_0 || %3_0 \n!%1_0 || !%2_0 || !%3_0 \n!%2_0 || %3_0 || %1_0 \n%2_0 || !%3_0 || %1_0\n").arg(__z).arg(__x).arg(__y);
    result.append(sum0);
    QString carry0 = QString("!%1_0 || %2_0 \n!%1_0 || %3_0 \n!%2_0 || !%3_0 || %1_0\n").arg(carry).arg(__x).arg(__y);
    result.append(carry0);
    for (int index = 1; index < BIT_LENGTH; index++)
    {
        QString sumIndex = QString("!%1_%5 || %2_%5 || !%3_%5 || !%4_%6 \n!%1_%5 || !%2_%5 || %3_%5 || !%4_%6 \n!%1_%5 || !%2_%5 || !%3_%5 || %4_%6 \n!%1_%5 || %2_%5 || %3_%5 || %4_%6 \n!%2_%5 || %3_%5 || %4_%6 || %1_%5 \n%2_%5 || !%3_%5 || %4_%6 || %1_%5 \n%2_%5 || %3_%5 || !%4_%6 || %1_%5 \n!%2_%5 || !%3_%5 || !%4_%6 || %1_%5\n")
                .arg(__z).arg(__x).arg(__y).arg(carry).arg(index).arg(index-1);
        result.append(sumIndex);

        QString carryIndex = QString("!%1_%4 || %2_%4 || %3_%4 \n!%1_%4 || %2_%4 || %1_%5 \n!%1_%4 || %3_%4 || %1_%5 \n!%2_%4 || !%3_%4 || %1_%4 \n!%2_%4 || !%1_%5 || %1_%4 \n!%3_%4 || !%1_%5 || %1_%4\n").arg(carry).arg(__x).arg(__y).arg(index).arg(index-1);
        result.append(carryIndex);
    }

    QString sumAtBitEnd = QString("!%1_%3 || %2_%4 \n!%2_%4 || %1_%3\n").arg(__z).arg(carry).arg(BIT_LENGTH).arg(BIT_LENGTH-1);
    result.append(sumAtBitEnd);

    bool isXInt = false, isYInt = false, isZInt = false;
    int x_value = __x.toInt(&isXInt);
    int y_value = __y.toInt(&isYInt);
    int z_value = __z.toInt(&isZInt);

//    qDebug() << isXInt << " " << isYInt << " " << isZInt;

    QString binary;
    if (isXInt)
    {
        binary = convertToBinary(x_value);
        for (int i = BIT_LENGTH - 1; i >= 0; i--)
        {
            QString xIndex = QString(binary.at(i)).toInt() == 0 ? "!" : "";
            xIndex.append(QString("%1_%2\n").arg(__x).arg(BIT_LENGTH - i - 1));
            result.append(xIndex);
        }
    }

    if (isYInt)
    {
        binary = convertToBinary(y_value);
        for (int i = BIT_LENGTH - 1; i >= 0; i--)
        {
            QString yIndex = QString(binary.at(i)).toInt() == 0 ? "!" : "";
            yIndex.append(QString("%1_%2\n").arg(__y).arg(BIT_LENGTH - i - 1));
            result.append(yIndex);
        }
    }

    if (isZInt)
    {
        binary = convertToBinary(z_value);
        for (int i = BIT_LENGTH - 1; i >= 0; i--)
        {
            QString zIndex = QString(binary.at(i)).toInt() == 0 ? "!" : "";
            zIndex.append(QString("%1_%2\n").arg(__z).arg(BIT_LENGTH - i - 1));
            result.append(zIndex);
        }
    }

    return result;
}

QString convertToBinary(int integer)
{
    QString res = QString::number(integer, 2);
    int lengthOfBinary = res.length();
    if (lengthOfBinary < BIT_LENGTH)
    {
        for (int i = 0; i < (BIT_LENGTH - lengthOfBinary); i++ )
        {
            res.push_front("0");
        }

    }
    return res;
}

void generateSumCnfFlie()
{

    QFile file(ruleFileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qDebug() << "cannot open file";
        return;
    }
    QTextStream stream(&file);

    QFile clauseFile(cnfFileName);
    if (!clauseFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qDebug() << "cannot open file";
        return;
    }
    QTextStream cnfStream(&clauseFile);
    QString resultCnfClause = "";
    int numberOfClause = 0;
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        QStringList elements = line.split(" || ");
//        qDebug() << "elements:" << elements;
        QString cnfLine = "";
        foreach (QString ele, elements)
        {
            bool isNOT = false;
            QString varLogical = ele.trimmed();
            if (ele.contains("!"))
            {
                varLogical = varLogical.split("!").at(1);
                isNOT = true;
            }
            if (!mapVariables.values().contains(varLogical))
            {
                mapVariables.insert(globalIndex++,varLogical);
            }
            cnfLine.append(QString("%1 ").arg(isNOT ? - mapVariables.key(varLogical) : mapVariables.key(varLogical)));
        }
        cnfLine.append("0");
        numberOfClause++;
        resultCnfClause.append(cnfLine).append("\n");
    }
    resultCnfClause.push_front(QString("p cnf %1 %2\n").arg(mapVariables.keys().count()).arg(numberOfClause));
    cnfStream << resultCnfClause;
    file.close();
    clauseFile.close();
}

void runSATSolver(string resultFileName, SimpSolver& S)
{
    try {
            setUsageHelp("USAGE: %s [options] <input-file> <result-output-file>\n\n  where input may be either in plain or gzipped DIMACS.\n");
            setX86FPUPrecision();

            // Extra options:
            //
            IntOption    verb   ("MAIN", "verb",   "Verbosity level (0=silent, 1=some, 2=more).", 1, IntRange(0, 2));
            BoolOption   pre    ("MAIN", "pre",    "Completely turn on/off any preprocessing.", true);
            BoolOption   solve  ("MAIN", "solve",  "Completely turn on/off solving after preprocessing.", true);
            StringOption dimacs ("MAIN", "dimacs", "If given, stop after preprocessing and write the result to this file.");
            IntOption    cpu_lim("MAIN", "cpu-lim","Limit on CPU time allowed in seconds.\n", 0, IntRange(0, INT32_MAX));
            IntOption    mem_lim("MAIN", "mem-lim","Limit on memory usage in megabytes.\n", 0, IntRange(0, INT32_MAX));
            BoolOption   strictp("MAIN", "strict", "Validate DIMACS header during parsing.", false);

//            SimpSolver  S = simpleSolver;
            double      initial_time = cpuTime();

            if (!pre) S.eliminate(true);

            S.verbosity = verb;

            solver = &S;
            // Use signal handlers that forcibly quit until the solver will be able to respond to
            // interrupts:
            sigTerm(SIGINT_exit);

            // Try to set resource limits:
            if (cpu_lim != 0) limitTime(cpu_lim);
            if (mem_lim != 0) limitMemory(mem_lim);

            gzFile in = gzopen(cnfFileName.toStdString().c_str(), "rb");

            if (S.verbosity > 0){
                printf("============================[ Problem Statistics ]=============================\n");
                printf("|                                                                             |\n"); }

            parse_DIMACS(in, S, (bool)strictp);
            gzclose(in);
            FILE* res = fopen(resultFileName.c_str(), "wb");

            if (S.verbosity > 0){
                printf("|  Number of variables:  %12d                                         |\n", S.nVars());
                printf("|  Number of clauses:    %12d                                         |\n", S.nClauses()); }

            double parsed_time = cpuTime();
            if (S.verbosity > 0)
                printf("|  Parse time:           %12.2f s                                       |\n", parsed_time - initial_time);

            // Change to signal-handlers that will only notify the solver and allow it to terminate
            // voluntarily:
            sigTerm(SIGINT_interrupt);

            S.eliminate(true);
            double simplified_time = cpuTime();
            if (S.verbosity > 0){
                printf("|  Simplification time:  %12.2f s                                       |\n", simplified_time - parsed_time);
                printf("|                                                                             |\n"); }

            if (!S.okay()){
                if (res != NULL) fprintf(res, "UNSAT\n"), fclose(res);
                if (S.verbosity > 0){
                    printf("===============================================================================\n");
                    printf("Solved by simplification\n");
                    S.printStats();
                    printf("\n"); }
                printf("UNSATISFIABLE\n");
//                exit(20);
            }

            lbool ret = l_Undef;

            if (solve){
                vec<Lit> dummy;
                ret = S.solveLimited(dummy);
            }else if (S.verbosity > 0)
                printf("===============================================================================\n");

            if (dimacs && ret == l_Undef)
                S.toDimacs((const char*)dimacs);

            if (S.verbosity > 0){
                S.printStats();
                printf("\n"); }
            printf(ret == l_True ? "SATISFIABLE\n" : ret == l_False ? "UNSATISFIABLE\n" : "INDETERMINATE\n");
            if (res != NULL){
                if (ret == l_True){
                    fprintf(res, "SAT\n");
                    for (int i = 0; i < S.nVars(); i++)
                        if (S.model[i] != l_Undef)
                            fprintf(res, "%s%s%d", (i==0)?"":" ", (S.model[i]==l_True)?"":"-", i+1);
                    fprintf(res, " 0\n");
                }else if (ret == l_False)
                    fprintf(res, "UNSAT\n");
                else
                    fprintf(res, "INDET\n");
                fclose(res);
            }

        } catch (OutOfMemoryException&){
            printf("===============================================================================\n");
            printf("INDETERMINATE\n");
//            exit(0);
        }
}





void multiNotContainStar(QString input, int &coef, QString &varText)
{
    QString coefText="";
    input = input.trimmed();
    for(int i = 0; i < input.length(); i++)
    {
        QChar charAtI = input.at(i);
        if (charAtI.isNumber())
        {
            coefText.append(charAtI);
        }
        else
        {
            varText = input.mid(i, input.length()-i);
            varText = varText.trimmed();
            break;
        }
    }
    if (coefText!="")
        coef = coefText.toInt();
    else
        coef = 1;
}


void multiContainStar(QString input, int &coef, QString &varText)
{
    QStringList resultAfterSplitStar = input.simplified().split("*");

//    qDebug() << resultAfterSplitStar;
    foreach (QString str, resultAfterSplitStar)
    {
        str = str.trimmed();
        bool isOk = false;
        int value = str.toInt(&isOk);
        if (isOk)
        {
            coef *= value;
        }
        else
        {
            int tempCoef = 1;
            multiNotContainStar(str, tempCoef, varText);
            coef *= tempCoef;
            varText = varText.trimmed();
        }
    }
}

void multiplySolver(QTextStream &textStream, QString input = "")
{
    bool isNumber = false;
    input.simplified().trimmed().toInt(&isNumber);
    if (isNumber)
    {
//        qDebug() << "is number, not multiply exp";
        return;
    }

    if (input.length() == 1)
    {

//        qDebug() << "multi with 1";
        return;
    }

    int coef = 1;
    QString varText;
    if (input.contains("*"))
    {
        multiContainStar(input, coef, varText);
    }
    else
    {
        multiNotContainStar(input, coef, varText);
    }

    if (coef >=2)
    {
        QString sumLooping = "2"+varText;
        textStream << generateSumRule(varText,varText,sumLooping);
        for (int i = 1; i <= (coef - 2); i++)
        {
            QString oldSumLooping = sumLooping;
            sumLooping = QString::number(2+i)+varText;
            textStream << generateSumRule(oldSumLooping,varText,sumLooping);
        }
    }

}

void handlePlusAndMinus(QString input, QStringList& positiveVarList, QStringList& negativeVarList)
{
    input = input.simplified().trimmed();
    QStringList listAfterRemovePlus = input.split("+");
    foreach (QString str, listAfterRemovePlus)
    {
        str = str.simplified().trimmed();
        if (str.contains("-"))
        {
            int startIndex = 0;
            for (startIndex; startIndex < str.length(); )
            {
                int pos = startIndex;
                bool isNegative = str.at(pos) == '-';
                QString varText;
                int startI = (isNegative ? (pos + 1) : pos);
                for (int i = startI ; i < str.length(); i++)
                {
                    if (str.at(i) == '-')
                    {
                        startIndex = i;
                        varText = str.mid(startI, startIndex - startI);
                        break;
                    }
                    if (i == str.length() - 1)
                    {
                        startIndex = str.length();
                        varText = str.mid(startI, startIndex - startI);
                        break;
                    }
                }

                if (isNegative)
                    negativeVarList.append(varText.simplified().trimmed());
                else
                    positiveVarList.append(varText.simplified().trimmed());
            }
        }
        else
        {
            positiveVarList.append(str);
        }
    }

//    qDebug() << "positiveList:"<<positiveVarList;
//    qDebug() << "negativeList:"<<negativeVarList;
}

void generateNormalizationForm(QString input, QStringList& leftSide, QStringList& rightSide)
{
    input = input.simplified().trimmed();

    QStringList listEqualRemoved = input.split("=");
    if (listEqualRemoved.count() > 1)
    {
        QString leftText = listEqualRemoved.at(0);
        QString rightText = listEqualRemoved.at(1);

        QStringList leftPositiveList, leftNegativeList;
        handlePlusAndMinus(leftText, leftPositiveList, leftNegativeList);

        QStringList rightPositiveList, rightNegativeList;
        handlePlusAndMinus(rightText, rightPositiveList, rightNegativeList);

        leftSide.append(leftPositiveList);
        leftSide.append(rightNegativeList);

        rightSide.append(rightPositiveList);
        rightSide.append(leftNegativeList);
    }
    else
    {
        qDebug() << "Exp incorrect!";
        return;
    }

    foreach (QString str, leftSide) {
        if (str == "0")
            leftSide.removeOne(str);
    }

    foreach (QString str, rightSide) {
        if (str == "0")
            rightSide.removeOne(str);
    }
}



QString sumOfSide(QTextStream& textStream, QStringList varsOnSide)
{
    if (varsOnSide.isEmpty())
        return "0";
    else if (varsOnSide.count() == 1)
    {
        multiplySolver(textStream, varsOnSide.at(0));
        return varsOnSide.at(0);
    }
    else
    {
        QString sumTemp = varsOnSide.at(0);
        multiplySolver(textStream, sumTemp);

        for (int i = 1; i < varsOnSide.count(); i++)
        {
            QString varAtI = varsOnSide.at(i);
            multiplySolver(textStream, varAtI);

            QString oldSumTemp = sumTemp;
            sumTemp = oldSumTemp + "+" + varAtI;
            textStream << generateSumRule(oldSumTemp, varAtI, sumTemp);
        }
        return sumTemp;
    }
}

void expressionSolver(QTextStream& textStream, QString input = 0)
{
    qDebug() << input;
    QStringList leftSideEles, rightSideEles;
    generateNormalizationForm(input, leftSideEles, rightSideEles);

    QString sumOfLeftSide = sumOfSide(textStream, leftSideEles);
    QString sumOfRightSide = sumOfSide(textStream, rightSideEles);

    textStream << generateSumRule(sumOfLeftSide, "0", sumOfRightSide);
}


int main()
{


    QFile ruleFlie(ruleFileName);
    if (!ruleFlie.open(QIODevice::WriteOnly))
        return 0;
    QTextStream ruleStreamWrite(&ruleFlie);

    expressionSolver(ruleStreamWrite, "x + y = 5");
    expressionSolver(ruleStreamWrite, "x - y = 3");
    ruleFlie.close();

    generateSumCnfFlie();


    for (int i = 0; i < mapVariables.keys().count(); i++)
    {
        int _key = mapVariables.keys().at(i);
        QString _bit = mapVariables.value(_key);

        qDebug() << "_key: " << _key << " _bit: " << _bit;
    }



    SimpSolver simpleSolver;
    runSATSolver("result.txt", simpleSolver);


    QString SATResult = "";
    for (int i = 0; i < simpleSolver.nVars(); i++)
        if (simpleSolver.model[i] != l_Undef)
        {
            QString outputs = QString("%1%2%3").arg((i==0)?"":" ").arg((simpleSolver.model[i]==l_True)?"":"-").arg(i+1);
            SATResult.append(outputs);
        }
//    qDebug() << SATResult;

    QString resultX = "", resultY = "";
    for (int i = 0; i < BIT_LENGTH; i++)
    {
        int varSat_x_i = mapVariables.key(QString("x_%1").arg(i));
        resultX.push_front(simpleSolver.model[varSat_x_i - 1] == l_True ? "1" : "0");

        int varSat_y_i = mapVariables.key(QString("y_%1").arg(i));
        resultY.push_front(simpleSolver.model[varSat_y_i - 1] == l_True ? "1" : "0");
    }

    bool test = false;
    qDebug() << "result: x = " << resultX.toInt(&test, 2) << " y = " << resultY.toInt(&test, 2);










    /*************************** done generate cnf file and implement minisat to solve the problem
    QFile ruleFlie(ruleFileName);
    if (!ruleFlie.open(QIODevice::WriteOnly))
        return 0;

    QTextStream ruleStreamWrite(&ruleFlie);
    ruleStreamWrite << generateSumRule("x", "y", "x+y");
    ruleStreamWrite << generateSumRule("x+y", "z", "20");
    ruleStreamWrite << generateSumRule("x", "y", "z");
    ruleStreamWrite << generateSumRule("y", "z", "16");
    ruleFlie.close();


    generateSumCnfFlie();

    for (int i = 0; i < mapVariables.keys().count(); i++)
    {
        int _key = mapVariables.keys().at(i);
        QString _bit = mapVariables.value(_key);

        qDebug() << "_key: " << _key << " _bit: " << _bit;
    }

    SimpSolver simpleSolver;
    runSATSolver("result.txt", simpleSolver);


    QString SATResult = "";
    for (int i = 0; i < simpleSolver.nVars(); i++)
        if (simpleSolver.model[i] != l_Undef)
        {
            QString outputs = QString("%1%2%3").arg((i==0)?"":" ").arg((simpleSolver.model[i]==l_True)?"":"-").arg(i+1);
            SATResult.append(outputs);
        }
    qDebug() << SATResult;

    QString resultX = "", resultY = "";
    for (int i = 0; i < BIT_LENGTH; i++)
    {
        int varSat_x_i = mapVariables.key(QString("x_%1").arg(i));
        resultX.push_front(simpleSolver.model[varSat_x_i - 1] == l_True ? "1" : "0");

        int varSat_y_i = mapVariables.key(QString("y_%1").arg(i));
        resultY.push_front(simpleSolver.model[varSat_y_i - 1] == l_True ? "1" : "0");
    }

    bool test = false;
    qDebug() << "result: x = " << resultX.toInt(&test, 2) << " y = " << resultY.toInt(&test, 2);
    ***********************************/





    return 0;
}
