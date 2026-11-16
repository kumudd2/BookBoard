

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>

#ifdef _WIN32
#include <direct.h>
#include <conio.h>
#define MAKE_DIR(p) _mkdir(p)
#else
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#define MAKE_DIR(p) mkdir(p, 0755)
#endif

/* ------------------------- Constants ------------------------------ */
#define STUDENT_FILE "students.txt"
#define LIBRARIAN_FILE "librarians.txt"
#define BOOK_FILE "books.txt"
#define EBOOK_FILE "ebooks.txt"
#define ISSUE_FILE "issued_books.txt"
#define TRANS_FILE "transactions.txt"
#define WAIT_FILE "waitlist.txt"
#define TEMP_FILE "temp.tmp"
#define INFO_FILE "projectinfo.txt"
#define EBOOK_DIR "ebooks"

#define LEN_ID 20    /* user ids                        */
#define LEN_NAME 50  /* names, course, author, category */
#define LEN_TEXT 100 /* titles, e-mail, file path       */
#define LEN_DESC 200 /* e-book description              */
#define LEN_PASS 30
#define LEN_LOG 300 /* one transaction line            */

#define MAX_STUDENTS 200
#define MAX_LIBRARIANS 20
#define MAX_EBOOKS 100
#define MAX_ISSUES 1000
#define MAX_ID 999999

#define LOAN_DAYS 14      /* a book can be kept this many days */
#define FINE_PER_DAY 2    /* Rs. per late day                  */
#define MAX_PER_STUDENT 3 /* books one student may hold        */
#define STACK_SIZE 10     /* recent activities remembered      */
#define LINES_PER_PAGE 15 /* e-book page size                  */
#define LIBRARIAN_KEY "LIB-2026"

/* ------------------------- Structures ----------------------------- */
typedef struct
{
    char id[LEN_ID];
    char name[LEN_NAME];
    char course[LEN_NAME];
    int semester;
    char email[LEN_TEXT];
    unsigned long passHash;
} Student;

typedef struct
{
    char id[LEN_ID];
    unsigned long passHash;
} Librarian;

typedef struct Book
{ /* node of the book linked list */
    int id;
    char title[LEN_TEXT];
    char author[LEN_NAME];
    char category[LEN_NAME];
    int total;
    int available;
    struct Book *next;
} Book;

typedef struct
{
    int id;
    char title[LEN_TEXT];
    char author[LEN_NAME];
    char category[LEN_NAME];
    char description[LEN_DESC];
    char file[LEN_TEXT];
    int available; /* 1 = readable, 0 = not readable */
} EBook;

typedef struct
{
    int issueId;
    char studentId[LEN_ID];
    int bookId;
    time_t issueDate;
    time_t dueDate;
    int returned; /* 0 = still with student, 1 = returned */
} Issue;

typedef struct WaitNode
{ /* node of the waiting queue */
    char studentId[LEN_ID];
    int bookId;
    struct WaitNode *next;
} WaitNode;

/* ------------------------- Global data ---------------------------- */
static Student students[MAX_STUDENTS];
static int studentCount = 0;
static Librarian librarians[MAX_LIBRARIANS];
static int librarianCount = 0;
static EBook ebooks[MAX_EBOOKS];
static int ebookCount = 0;
static Issue issues[MAX_ISSUES];
static int issueCount = 0;

static Book *bookHead = NULL;      /* linked list of books  */
static WaitNode *waitFront = NULL; /* queue: remove here    */
static WaitNode *waitRear = NULL;  /* queue: add here       */

static char recentActivity[STACK_SIZE][LEN_LOG]; /* stack storage   */
static int stackTop = 0;                         /* items in stack  */

static char currentUser[LEN_ID] = ""; /* who is logged in      */

/* =====================================================================
 *  SECTION 1 : SMALL HELPER FUNCTIONS
 * ===================================================================== */

/* Safe string copy: never writes outside the destination. */
static void copyStr(char *dest, const char *src, size_t size)
{
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

/* Remove spaces at the start and end of a string. */
static void trim(char *s)
{
    char *start = s;
    while (isspace((unsigned char)*start))
        start++;
    if (start != s)
        memmove(s, start, strlen(start) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
}

/* Read one line from the keyboard.
 * Returns 1 if the line fitted in the buffer, 0 if it was too long.
 * If the keyboard input is closed (Ctrl+D / Ctrl+Z) the program ends.   */
static int getLine(char *buf, int size)
{
    if (fgets(buf, size, stdin) == NULL)
    {
        printf("\nInput closed. Goodbye!\n");
        exit(0);
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
    {
        buf[--len] = '\0';
        if (len > 0 && buf[len - 1] == '\r')
            buf[--len] = '\0';
        return 1;
    }
    int c, extra = 0; /* line was too long: throw away the rest */
    while ((c = getchar()) != '\n' && c != EOF)
        extra = 1;
    return !extra;
}

/* Ask for non-empty text. Repeats until the input is valid.
 * The '|' character is replaced because it separates fields in our files. */
static void readText(const char *prompt, char *buf, int size)
{
    while (1)
    {
        printf("%s", prompt);
        if (!getLine(buf, size))
        {
            printf("  Too long! Maximum %d characters.\n", size - 1);
            continue;
        }
        trim(buf);
        if (buf[0] == '\0')
        {
            printf("  This field cannot be empty.\n");
            continue;
        }
        for (char *p = buf; *p; p++)
            if (*p == '|')
                *p = '/';
        return;
    }
}

/* Read a password: the typed characters are shown as '*'.
 * Same rules as readText (not empty, '|' replaced, length checked).
 * Backspace works. Works on Windows (conio) and Linux/macOS (termios). */
static void readSecret(const char *prompt, char *buf, int size)
{
    while (1)
    {
        int len = 0, tooLong = 0;
        printf("%s", prompt);
        fflush(stdout);

#ifdef _WIN32
        int c;
        while ((c = _getch()) != '\r' && c != '\n')
        {
            if (c == 3)
            {
                printf("\n");
                exit(0);
            } /* Ctrl+C */
            if (c == 0 || c == 224)
            {
                _getch();
                continue;
            } /* arrow / function keys */
            if (c == '\b')
            {
                if (len > 0)
                {
                    len--;
                    printf("\b \b");
                }
                continue;
            }
            if (len < size - 1)
            {
                buf[len++] = (char)c;
                printf("*");
            }
            else
                tooLong = 1;
        }
#else
        struct termios oldt, newt;
        int haveTerm = (tcgetattr(STDIN_FILENO, &oldt) == 0);
        if (haveTerm)
        {
            newt = oldt;
            newt.c_lflag &= ~(tcflag_t)(ECHO | ICANON);
            newt.c_cc[VMIN] = 1;
            newt.c_cc[VTIME] = 0;
            tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        }
        int c;
        while ((c = getchar()) != '\n' && c != '\r')
        {
            if (c == EOF)
            {
                if (haveTerm)
                    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
                printf("\nInput closed. Goodbye!\n");
                exit(0);
            }
            if (c == 127 || c == '\b')
            {
                if (len > 0)
                {
                    len--;
                    printf("\b \b");
                    fflush(stdout);
                }
                continue;
            }
            if (len < size - 1)
            {
                buf[len++] = (char)c;
                printf("*");
                fflush(stdout);
            }
            else
                tooLong = 1;
        }
        if (haveTerm)
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
        buf[len] = '\0';
        printf("\n");

        if (tooLong)
        {
            printf("  Too long! Maximum %d characters.\n", size - 1);
            continue;
        }
        trim(buf);
        if (buf[0] == '\0')
        {
            printf("  This field cannot be empty.\n");
            continue;
        }
        for (char *p = buf; *p; p++)
            if (*p == '|')
                *p = '/';
        return;
    }
}

/* Like readText, but pressing Enter is allowed (means "keep old value").
 * Returns 1 if the user typed something, 0 if the user pressed Enter. */
static int readOptional(const char *prompt, char *buf, int size)
{
    while (1)
    {
        printf("%s", prompt);
        if (!getLine(buf, size))
        {
            printf("  Too long! Maximum %d characters.\n", size - 1);
            continue;
        }
        trim(buf);
        if (buf[0] == '\0')
            return 0;
        for (char *p = buf; *p; p++)
            if (*p == '|')
                *p = '/';
        return 1;
    }
}

/* Read a whole number between min and max.
 * If allowBlank is 1, pressing Enter returns 0 and leaves *out unchanged. */
static int readIntCore(const char *prompt, int min, int max, int allowBlank, int *out)
{
    char buf[32], *end;
    while (1)
    {
        printf("%s", prompt);
        if (!getLine(buf, sizeof buf))
        {
            printf("  Invalid input.\n");
            continue;
        }
        trim(buf);
        if (buf[0] == '\0' && allowBlank)
            return 0;
        long v = strtol(buf, &end, 10);
        if (buf[0] == '\0' || *end != '\0' || v < min || v > max)
        {
            printf("  Please enter a number between %d and %d.\n", min, max);
            continue;
        }
        *out = (int)v;
        return 1;
    }
}

static int readInt(const char *prompt, int min, int max)
{
    int v = 0;
    readIntCore(prompt, min, max, 0, &v);
    return v;
}

/* Ask a yes/no question. Returns 1 for yes. */
static int yesNo(const char *prompt)
{
    char buf[10];
    while (1)
    {
        printf("%s (y/n): ", prompt);
        if (!getLine(buf, sizeof buf))
            continue;
        trim(buf);
        if (tolower((unsigned char)buf[0]) == 'y' && buf[1] == '\0')
            return 1;
        if (tolower((unsigned char)buf[0]) == 'n' && buf[1] == '\0')
            return 0;
        printf("  Please type y or n.\n");
    }
}

/* A valid ID has only letters, digits, '-' and '_' (no spaces). */
static int isValidId(const char *s)
{
    if (s[0] == '\0')
        return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_')
            return 0;
    return 1;
}

static void readId(const char *prompt, char *buf, int size)
{
    while (1)
    {
        readText(prompt, buf, size);
        if (isValidId(buf))
            return;
        printf("  Use only letters, digits, '-' and '_' (no spaces).\n");
    }
}

/* Very simple e-mail check: something@something.something, no spaces. */
static void readEmail(const char *prompt, char *buf, int size)
{
    while (1)
    {
        readText(prompt, buf, size);
        char *at = strchr(buf, '@');
        if (at && at != buf && strchr(at, '.') && !strchr(buf, ' ') &&
            at[1] != '.' && buf[strlen(buf) - 1] != '.')
            return;
        printf("  Please enter a valid e-mail (example: name@college.edu).\n");
    }
}

/* Case-insensitive "does text contain word?" used by the search features. */
static int containsIgnoreCase(const char *text, const char *word)
{
    size_t wl = strlen(word);
    if (wl == 0)
        return 1;
    for (; *text; text++)
    {
        size_t i = 0;
        while (i < wl && text[i] &&
               tolower((unsigned char)text[i]) == tolower((unsigned char)word[i]))
            i++;
        if (i == wl)
            return 1;
    }
    return 0;
}

/* Split "a|b|c" into pieces (in place). Returns how many pieces. */
static int splitFields(char *line, char *fields[], int maxFields)
{
    int n = 0;
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';
    fields[n++] = line;
    for (char *p = line; *p && n < maxFields; p++)
    {
        if (*p == '|')
        {
            *p = '\0';
            fields[n++] = p + 1;
        }
    }
    return n;
}

/* Password helper: a simple hash (djb2). See the security note above. */
static unsigned long hashPassword(const char *s)
{
    unsigned long h = 5381;
    while (*s)
    {
        h = h * 33 + (unsigned char)*s;
        s++;
    }
    return h;
}

static void formatDate(time_t t, char *out, size_t size)
{
    struct tm *tmv = localtime(&t);
    if (tmv)
        strftime(out, size, "%d-%m-%Y", tmv);
    else
        copyStr(out, "unknown", size);
}

static int fileExists(const char *name)
{
    FILE *fp = fopen(name, "r");
    if (fp)
    {
        fclose(fp);
        return 1;
    }
    return 0;
}

/* =====================================================================
 *  SECTION 2 : STACK (recent activity) AND TRANSACTION LOG
 * ===================================================================== */

/* PUSH: add newest activity on top. When full, the oldest one drops out. */
static void pushActivity(const char *text)
{
    if (stackTop == STACK_SIZE)
    {
        for (int i = 1; i < STACK_SIZE; i++)
            strcpy(recentActivity[i - 1], recentActivity[i]);
        stackTop--;
    }
    copyStr(recentActivity[stackTop++], text, LEN_LOG);
}

/* Show the stack from top (newest) to bottom (oldest). */
static void showRecentActivity(void)
{
    printf("\n--- Recent activity (newest first, this session only) ---\n");
    if (stackTop == 0)
    {
        printf("Nothing has happened yet in this session.\n");
        return;
    }
    for (int i = stackTop - 1; i >= 0; i--)
        printf("%2d. %s\n", stackTop - i, recentActivity[i]);
}

/* Write one line to transactions.txt (append mode keeps old lines safe)
 * and push the same line on the recent-activity stack. */
static void logTransaction(const char *type, const char *fmt, ...)
{
    char details[200], entry[LEN_LOG], when[32];
    va_list args;
    va_start(args, fmt);
    vsnprintf(details, sizeof details, fmt, args);
    va_end(args);

    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    if (tmv)
        strftime(when, sizeof when, "%d-%m-%Y %H:%M:%S", tmv);
    else
        copyStr(when, "unknown time", sizeof when);

    snprintf(entry, sizeof entry, "%s | %-14s | %s | %s",
             when, type, currentUser[0] ? currentUser : "system", details);

    FILE *fp = fopen(TRANS_FILE, "a");
    if (fp)
    {
        fprintf(fp, "%s\n", entry);
        fclose(fp);
    }
    else
    {
        printf("Warning: could not write to %s\n", TRANS_FILE);
    }
    pushActivity(entry);
}

/* Print the whole permanent history, line by line. */
static void showTransactionFile(void)
{
    FILE *fp = fopen(TRANS_FILE, "r");
    char line[LEN_LOG + 50];
    int count = 0;
    if (!fp)
    {
        printf("Error: cannot open %s\n", TRANS_FILE);
        return;
    }
    printf("\n--- Full transaction history ---\n");
    while (fgets(line, sizeof line, fp))
    {
        printf("%s", line);
        if (!strchr(line, '\n'))
            printf("\n");
        count++;
    }
    fclose(fp);
    if (count == 0)
        printf("No transactions recorded yet.\n");
}

/* =====================================================================
 *  SECTION 3 : SAVING FILES SAFELY
 *  Rule: write everything to temp.tmp first. Only if that worked,
 *  replace the real file. A failed write never destroys good data.
 * ===================================================================== */

static int finishSave(FILE *fp, const char *target)
{
    int bad = ferror(fp);
    if (fclose(fp) != 0)
        bad = 1;
    if (bad)
    {
        remove(TEMP_FILE);
        printf("Error: could not write %s. Old data kept.\n", target);
        return 0;
    }
    remove(target); /* needed on Windows */
    if (rename(TEMP_FILE, target) != 0)
    {
        printf("Error: could not replace %s\n", target);
        return 0;
    }
    return 1;
}

static FILE *startSave(void)
{
    FILE *fp = fopen(TEMP_FILE, "w");
    if (!fp)
        printf("Error: cannot create temporary file.\n");
    return fp;
}

static int saveStudents(void)
{
    FILE *fp = startSave();
    if (!fp)
        return 0;
    for (int i = 0; i < studentCount; i++)
        fprintf(fp, "%s|%s|%s|%d|%s|%lu\n", students[i].id, students[i].name,
                students[i].course, students[i].semester, students[i].email,
                students[i].passHash);
    return finishSave(fp, STUDENT_FILE);
}

static int saveLibrarians(void)
{
    FILE *fp = startSave();
    if (!fp)
        return 0;
    for (int i = 0; i < librarianCount; i++)
        fprintf(fp, "%s|%lu\n", librarians[i].id, librarians[i].passHash);
    return finishSave(fp, LIBRARIAN_FILE);
}

static int saveBooks(void)
{
    FILE *fp = startSave();
    if (!fp)
        return 0;
    for (Book *b = bookHead; b; b = b->next)
        fprintf(fp, "%d|%s|%s|%s|%d|%d\n", b->id, b->title, b->author,
                b->category, b->total, b->available);
    return finishSave(fp, BOOK_FILE);
}

static int saveEbooks(void)
{
    FILE *fp = startSave();
    if (!fp)
        return 0;
    for (int i = 0; i < ebookCount; i++)
        fprintf(fp, "%d|%s|%s|%s|%s|%s|%d\n", ebooks[i].id, ebooks[i].title,
                ebooks[i].author, ebooks[i].category, ebooks[i].description,
                ebooks[i].file, ebooks[i].available);
    return finishSave(fp, EBOOK_FILE);
}

static int saveIssues(void)
{
    FILE *fp = startSave();
    if (!fp)
        return 0;
    for (int i = 0; i < issueCount; i++)
        fprintf(fp, "%d|%s|%d|%lld|%lld|%d\n", issues[i].issueId,
                issues[i].studentId, issues[i].bookId,
                (long long)issues[i].issueDate, (long long)issues[i].dueDate,
                issues[i].returned);
    return finishSave(fp, ISSUE_FILE);
}

static int saveWaitlist(void)
{
    FILE *fp = startSave();
    if (!fp)
        return 0;
    for (WaitNode *n = waitFront; n; n = n->next)
        fprintf(fp, "%s|%d\n", n->studentId, n->bookId);
    return finishSave(fp, WAIT_FILE);
}

/* =====================================================================
 *  SECTION 4 : LOADING FILES AT START-UP
 * ===================================================================== */

/* Book list helpers (linked list) */
static Book *newBookNode(int id, const char *title, const char *author,
                         const char *category, int total, int available)
{
    Book *b = malloc(sizeof(Book));
    if (!b)
    {
        printf("Out of memory!\n");
        exit(1);
    }
    b->id = id;
    copyStr(b->title, title, sizeof b->title);
    copyStr(b->author, author, sizeof b->author);
    copyStr(b->category, category, sizeof b->category);
    b->total = total;
    b->available = available;
    b->next = NULL;
    return b;
}

static void appendBook(Book *b) /* add at the END of the list */
{
    if (!bookHead)
    {
        bookHead = b;
        return;
    }
    Book *cur = bookHead;
    while (cur->next)
        cur = cur->next;
    cur->next = b;
}

static void enqueueWaiting(const char *sid, int bid) /* QUEUE: add at rear */
{
    WaitNode *n = malloc(sizeof(WaitNode));
    if (!n)
    {
        printf("Out of memory!\n");
        exit(1);
    }
    copyStr(n->studentId, sid, sizeof n->studentId);
    n->bookId = bid;
    n->next = NULL;
    if (waitRear)
        waitRear->next = n;
    else
        waitFront = n;
    waitRear = n;
}

static void loadStudents(void)
{
    FILE *fp = fopen(STUDENT_FILE, "r");
    char line[1024], *f[6];
    if (!fp)
        return;
    while (studentCount < MAX_STUDENTS && fgets(line, sizeof line, fp))
    {
        if (splitFields(line, f, 6) != 6)
            continue;
        Student *s = &students[studentCount++];
        copyStr(s->id, f[0], sizeof s->id);
        copyStr(s->name, f[1], sizeof s->name);
        copyStr(s->course, f[2], sizeof s->course);
        s->semester = atoi(f[3]);
        copyStr(s->email, f[4], sizeof s->email);
        s->passHash = strtoul(f[5], NULL, 10);
    }
    fclose(fp);
}

static void loadLibrarians(void)
{
    FILE *fp = fopen(LIBRARIAN_FILE, "r");
    char line[256], *f[2];
    if (!fp)
        return;
    while (librarianCount < MAX_LIBRARIANS && fgets(line, sizeof line, fp))
    {
        if (splitFields(line, f, 2) != 2)
            continue;
        copyStr(librarians[librarianCount].id, f[0], LEN_ID);
        librarians[librarianCount].passHash = strtoul(f[1], NULL, 10);
        librarianCount++;
    }
    fclose(fp);
}

static void loadBooks(void)
{
    FILE *fp = fopen(BOOK_FILE, "r");
    char line[1024], *f[6];
    if (!fp)
        return;
    while (fgets(line, sizeof line, fp))
    {
        if (splitFields(line, f, 6) != 6)
            continue;
        appendBook(newBookNode(atoi(f[0]), f[1], f[2], f[3], atoi(f[4]), atoi(f[5])));
    }
    fclose(fp);
}

static void loadEbooks(void)
{
    FILE *fp = fopen(EBOOK_FILE, "r");
    char line[1024], *f[7];
    if (!fp)
        return;
    while (ebookCount < MAX_EBOOKS && fgets(line, sizeof line, fp))
    {
        if (splitFields(line, f, 7) != 7)
            continue;
        EBook *e = &ebooks[ebookCount++];
        e->id = atoi(f[0]);
        copyStr(e->title, f[1], sizeof e->title);
        copyStr(e->author, f[2], sizeof e->author);
        copyStr(e->category, f[3], sizeof e->category);
        copyStr(e->description, f[4], sizeof e->description);
        copyStr(e->file, f[5], sizeof e->file);
        e->available = atoi(f[6]);
    }
    fclose(fp);
}

static void loadIssues(void)
{
    FILE *fp = fopen(ISSUE_FILE, "r");
    char line[512], *f[6];
    if (!fp)
        return;
    while (issueCount < MAX_ISSUES && fgets(line, sizeof line, fp))
    {
        if (splitFields(line, f, 6) != 6)
            continue;
        Issue *is = &issues[issueCount++];
        is->issueId = atoi(f[0]);
        copyStr(is->studentId, f[1], sizeof is->studentId);
        is->bookId = atoi(f[2]);
        is->issueDate = (time_t)strtoll(f[3], NULL, 10);
        is->dueDate = (time_t)strtoll(f[4], NULL, 10);
        is->returned = atoi(f[5]);
    }
    fclose(fp);
}

static void loadWaitlist(void)
{
    FILE *fp = fopen(WAIT_FILE, "r");
    char line[256], *f[2];
    if (!fp)
        return;
    while (fgets(line, sizeof line, fp))
    {
        if (splitFields(line, f, 2) != 2)
            continue;
        enqueueWaiting(f[0], atoi(f[1]));
    }
    fclose(fp);
}

/* =====================================================================
 *  SECTION 5 : FIRST-RUN SETUP AND SAMPLE DATA
 * ===================================================================== */

static void touchFile(const char *name) /* create the file if missing */
{
    FILE *fp = fopen(name, "a");
    if (fp)
        fclose(fp);
    else
        printf("Warning: cannot create %s\n", name);
}

static void writeSampleFile(const char *path, const char *const lines[])
{
    if (fileExists(path))
        return; /* never overwrite real files */
    FILE *fp = fopen(path, "w");
    if (!fp)
        return;
    for (int i = 0; lines[i]; i++)
        fprintf(fp, "%s\n", lines[i]);
    fclose(fp);
}

static void addSampleEbooks(void)
{
    static const char *const cBasics[] = {
        "LEARNING C BASICS", "=================", "",
        "Chapter 1: Your first program",
        "Every C program starts running from a function named main().",
        "A tiny program looks like this:",
        "    #include <stdio.h>",
        "    int main(void) { printf(\"Hello, library!\"); return 0; }", "",
        "Chapter 2: Variables",
        "A variable is a named box in memory that stores a value.",
        "Common types: int (whole numbers), float (decimals), char (letters).", "",
        "Chapter 3: Decisions and loops",
        "Use if / else to choose between actions.",
        "Use for, while and do-while to repeat actions.", "",
        "Chapter 4: Functions",
        "Break a big problem into small functions. Each does one job.", "",
        "Chapter 5: Files",
        "fopen() opens a file, fprintf() writes, fgets() reads, fclose() closes.",
        "Always check that fopen() did not return NULL.",
        "-- End of sample e-book --", NULL};
    static const char *const dsEasy[] = {
        "DATA STRUCTURES MADE EASY", "=========================", "",
        "A data structure is a way of organising data so it is easy to use.", "",
        "ARRAY",
        "  Items stored one after another. Fast access by position.",
        "  Size is fixed when you create it.", "",
        "LINKED LIST",
        "  Every item (node) points to the next one.",
        "  Easy to grow and shrink; you must walk through it to search.", "",
        "STACK  (Last In, First Out)",
        "  Like a pile of plates: you add and remove at the top.", "",
        "QUEUE  (First In, First Out)",
        "  Like a line at the library desk: the first person is served first.", "",
        "SORTING AND SEARCHING",
        "  Bubble sort swaps neighbours that are in the wrong order.",
        "  Linear search checks items one by one until it finds the target.",
        "-- End of sample e-book --", NULL};
    static const char *const study[] = {
        "SMART STUDY HABITS", "==================", "",
        "1. Plan your week",
        "   Write down what to study each day, even if only 45 minutes.", "",
        "2. Study in short blocks",
        "   Focus for 25 minutes, then rest for 5 minutes.", "",
        "3. Practise, do not only read",
        "   Solve problems and write code yourself. Reading alone is not enough.", "",
        "4. Teach someone else",
        "   If you can explain a topic simply, you understood it.", "",
        "5. Sleep well",
        "   A rested brain remembers more than a tired one.", "",
        "6. Use the library",
        "   Quiet space, good books and no distractions.",
        "-- End of sample e-book --", NULL};

    MAKE_DIR(EBOOK_DIR);
    writeSampleFile(EBOOK_DIR "/c_basics.txt", cBasics);
    writeSampleFile(EBOOK_DIR "/data_structures.txt", dsEasy);
    writeSampleFile(EBOOK_DIR "/study_habits.txt", study);

    EBook sample[3] = {
        {1, "Learning C Basics", "Library Demo Team", "Programming",
         "A very short introduction to C programming.",
         EBOOK_DIR "/c_basics.txt", 1},
        {2, "Data Structures Made Easy", "Library Demo Team", "Computer Science",
         "Arrays, lists, stacks and queues in simple words.",
         EBOOK_DIR "/data_structures.txt", 1},
        {3, "Smart Study Habits", "Library Demo Team", "Self Help",
         "Simple habits that help students study better.",
         EBOOK_DIR "/study_habits.txt", 1}};
    for (int i = 0; i < 3 && ebookCount < MAX_EBOOKS; i++)
        ebooks[ebookCount++] = sample[i];
    saveEbooks();
}

static void addSampleBooks(void)
{
    appendBook(newBookNode(101, "The C Programming Language", "Kernighan and Ritchie", "Programming", 3, 3));
    appendBook(newBookNode(102, "Let Us C", "Yashavant Kanetkar", "Programming", 4, 4));
    appendBook(newBookNode(103, "Data Structures Using C", "Reema Thareja", "Computer Science", 2, 2));
    appendBook(newBookNode(104, "Introduction to Algorithms", "Cormen, Leiserson, Rivest, Stein", "Computer Science", 2, 2));
    appendBook(newBookNode(105, "Wings of Fire", "A P J Abdul Kalam", "Biography", 1, 1));
    saveBooks();
}

static void createProjectInfo(void),
{
    /* Creates projectinfo.txt on the first run only (never overwrites). */
    static const char *const info[] = {

        "  =====================================================================",
        "                                BookBoard"
        "  =====================================================================",
        
        " ",
        "   First-run demo accounts",
        "       Librarian : ID = admin      Password = admin123",
        "       Student   : register from the main menu (option 3)",
        "       Librarian registration key (needed to create new librarians):",
        "                   LIB-2026",
        " ",
        "   FILES (created automatically in the current folder)",
        "       students.txt      id|name|course|semester|email|passwordHash",
        "       librarians.txt    id|passwordHash",
        "       books.txt         id|title|author|category|total|available",
        "       ebooks.txt        id|title|author|category|description|file|available",
        "       issued_books.txt  issueId|studentId|bookId|issueDate|dueDate|returned",
        "       transactions.txt  one line per important activity (append only)",
        "       waitlist.txt      studentId|bookId   (the waiting queue)",
        "       ebooks/           the plain-text e-book files",
        " ",
        "   DATA STRUCTURES USED (and why)",
        "       struct            Student, Librarian, Book, EBook, Issue group related",
        "                          data of one record into one variable.",
        "       Linked list       Books. The number of books keeps changing, so a",
        "                         list grows/shrinks without a fixed size limit.",
        "       Arrays            Students, librarians, e-books, issue records.",
        "                         Simple and fast when a maximum size is acceptable.",
        "       Queue (FIFO)      Students waiting for a book with no free copy.",
        "                         First student to wait is the first to be served.",
        "       Stack (LIFO)      \"Recent activity\" screen: the newest action is",
        "                         shown first. It is only a view; the permanent",
        "                         history stays safe in transactions.txt.",
        "       Linear search     Used to find books, students and e-books.",
        "       Bubble sort       Sorts the book list by title or by book ID.",
        " ",
        "   SECURITY NOTE (college demo only)",
        "       Passwords are stored as a simple hash (djb2), never as plain text.",
        "       djb2 is NOT secure enough for a real system; real software should",
        "       use bcrypt / Argon2 with a random salt. Typed passwords are shown",
        "       as * characters on screen.",
        "  =====================================================================",

        NULL};
    writeSampleFile(INFO_FILE, info);
}

static void initializeSystem(void)
{
    createProjectInfo(); /* projectinfo.txt (first run) */
    /* Remember which files are brand new (so we add demo data only once). */
    int freshLib = !fileExists(LIBRARIAN_FILE);
    int freshBooks = !fileExists(BOOK_FILE);
    int freshEbooks = !fileExists(EBOOK_FILE);

    MAKE_DIR(EBOOK_DIR); /* fails harmlessly if it exists */
    touchFile(STUDENT_FILE);
    touchFile(LIBRARIAN_FILE);
    touchFile(BOOK_FILE);
    touchFile(EBOOK_FILE);
    touchFile(ISSUE_FILE);
    touchFile(TRANS_FILE);
    touchFile(WAIT_FILE);

    loadStudents();
    loadLibrarians();
    loadBooks();
    loadEbooks();
    loadIssues();
    loadWaitlist();

    if (freshLib)
    {
        copyStr(librarians[0].id, "admin", LEN_ID);
        librarians[0].passHash = hashPassword("admin123");
        librarianCount = 1;
        saveLibrarians();
    }
    if (freshBooks)
        addSampleBooks();
    if (freshEbooks)
        addSampleEbooks();
}

/* =====================================================================
 *  SECTION 6 : LOOK-UP (SEARCH) FUNCTIONS  -  linear search
 * ===================================================================== */

static int findStudent(const char *id)
{
    for (int i = 0; i < studentCount; i++)
        if (strcmp(students[i].id, id) == 0)
            return i;
    return -1;
}

static int findLibrarian(const char *id)
{
    for (int i = 0; i < librarianCount; i++)
        if (strcmp(librarians[i].id, id) == 0)
            return i;
    return -1;
}

static Book *findBook(int id)
{
    for (Book *b = bookHead; b; b = b->next)
        if (b->id == id)
            return b;
    return NULL;
}

static int findEbook(int id)
{
    for (int i = 0; i < ebookCount; i++)
        if (ebooks[i].id == id)
            return i;
    return -1;
}

/* Find the issue record where this student still holds this book. */
static int findActiveIssue(const char *sid, int bid)
{
    for (int i = 0; i < issueCount; i++)
        if (!issues[i].returned && issues[i].bookId == bid &&
            strcmp(issues[i].studentId, sid) == 0)
            return i;
    return -1;
}

static int countActiveIssues(const char *sid)
{
    int n = 0;
    for (int i = 0; i < issueCount; i++)
        if (!issues[i].returned && strcmp(issues[i].studentId, sid) == 0)
            n++;
    return n;
}

static int nextIssueId(void)
{
    int max = 1000;
    for (int i = 0; i < issueCount; i++)
        if (issues[i].issueId > max)
            max = issues[i].issueId;
    return max + 1;
}

/* =====================================================================
 *  SECTION 7 : WAITING QUEUE OPERATIONS
 * ===================================================================== */

static int isWaiting(const char *sid, int bid)
{
    for (WaitNode *n = waitFront; n; n = n->next)
        if (n->bookId == bid && strcmp(n->studentId, sid) == 0)
            return 1;
    return 0;
}

/* The student at the front of the line for this book (or NULL). */
static WaitNode *firstWaitingFor(int bid)
{
    for (WaitNode *n = waitFront; n; n = n->next)
        if (n->bookId == bid)
            return n;
    return NULL;
}

static void removeWaiting(const char *studentId, int bid)
{
    char sid[LEN_ID];
    copyStr(sid, studentId, sizeof sid); /* copy: node may be freed */
    WaitNode *prev = NULL;
    for (WaitNode *cur = waitFront; cur; prev = cur, cur = cur->next)
    {
        if (cur->bookId == bid && strcmp(cur->studentId, sid) == 0)
        {
            if (prev)
                prev->next = cur->next;
            else
                waitFront = cur->next;
            if (cur == waitRear)
                waitRear = prev;
            free(cur);
            saveWaitlist();
            return;
        }
    }
}

static void removeAllWaitingForBook(int bid)
{
    WaitNode *n;
    char sid[LEN_ID];
    while ((n = firstWaitingFor(bid)) != NULL)
    {
        copyStr(sid, n->studentId, sizeof sid);
        removeWaiting(sid, bid);
    }
}

static void joinWaitingQueue(const char *sid, const Book *b)
{
    if (b->available > 0)
    {
        printf("Copies are available now, so no waiting is needed.\n");
        return;
    }
    if (findActiveIssue(sid, b->id) >= 0)
    {
        printf("You already have this book.\n");
        return;
    }
    if (isWaiting(sid, b->id))
    {
        printf("You are already in the waiting queue for this book.\n");
        return;
    }
    enqueueWaiting(sid, b->id);
    saveWaitlist();

    int position = 0; /* people ahead + you */
    for (WaitNode *n = waitFront; n; n = n->next)
        if (n->bookId == b->id)
            position++;
    logTransaction("WAIT_JOINED", "Student %s waits for book %d (%s)", sid, b->id, b->title);
    printf("Added to the waiting queue. Your position: %d\n", position);
}

static void showWaitingList(void)
{
    printf("\n--- Waiting queue (first come, first served) ---\n");
    if (!waitFront)
    {
        printf("No students are waiting.\n");
        return;
    }
    printf("%-5s %-15s %-8s %s\n", "No.", "Student ID", "Book ID", "Title");
    int i = 1;
    for (WaitNode *n = waitFront; n; n = n->next)
    {
        Book *b = findBook(n->bookId);
        printf("%-5d %-15s %-8d %s\n", i++, n->studentId, n->bookId, b ? b->title : "(deleted)");
    }
}

/* =====================================================================
 *  SECTION 8 : PASSWORDS AND ACCOUNTS
 * ===================================================================== */

static void readNewPassword(char *pass)
{
    char again[LEN_PASS];
    while (1)
    {
        readSecret("Choose password (min 4 characters): ", pass, LEN_PASS);
        if (strlen(pass) < 4)
        {
            printf("  Password is too short.\n");
            continue;
        }
        readSecret("Re-enter password: ", again, sizeof again);
        if (strcmp(pass, again) == 0)
            return;
        printf("  Passwords do not match. Try again.\n");
    }
}

/* Asks old password, then a new one. Returns 1 if the password changed. */
static int askChangePassword(unsigned long *hash)
{
    char oldPass[LEN_PASS], newPass[LEN_PASS];
    readSecret("Current password: ", oldPass, sizeof oldPass);
    if (hashPassword(oldPass) != *hash)
    {
        printf("Wrong current password.\n");
        return 0;
    }
    readNewPassword(newPass);
    *hash = hashPassword(newPass);
    return 1;
}

static void registerStudent(void)
{
    char id[LEN_ID], pass[LEN_PASS];
    Student s;
    printf("\n--- Student Registration ---\n");
    if (studentCount >= MAX_STUDENTS)
    {
        printf("Sorry, the student list is full.\n");
        return;
    }
    while (1)
    {
        readId("Student ID: ", id, sizeof id);
        if (findStudent(id) >= 0)
            printf("  This student ID already exists.\n");
        else
            break;
    }
    copyStr(s.id, id, sizeof s.id);
    readText("Full name: ", s.name, sizeof s.name);
    readText("Course (e.g. B.Tech CSE): ", s.course, sizeof s.course);
    s.semester = readInt("Semester (1-12): ", 1, 12);
    readEmail("E-mail: ", s.email, sizeof s.email);
    readNewPassword(pass);
    s.passHash = hashPassword(pass);

    students[studentCount++] = s;
    if (!saveStudents())
    {
        studentCount--;
        return;
    }
    copyStr(currentUser, "system", sizeof currentUser);
    logTransaction("STUDENT_ADDED", "Student %s (%s) registered", s.id, s.name);
    currentUser[0] = '\0';
    printf("Registration successful! You can now log in.\n");
}

static void registerLibrarian(void)
{
    char key[LEN_PASS], id[LEN_ID], pass[LEN_PASS];
    printf("\n--- Librarian Registration ---\n");
    readSecret("Enter librarian registration key: ", key, sizeof key);
    if (strcmp(key, LIBRARIAN_KEY) != 0)
    {
        printf("Wrong key. Registration denied.\n");
        return;
    }
    if (librarianCount >= MAX_LIBRARIANS)
    {
        printf("Sorry, the librarian list is full.\n");
        return;
    }
    while (1)
    {
        readId("New librarian ID: ", id, sizeof id);
        if (findLibrarian(id) >= 0)
            printf("  This librarian ID already exists.\n");
        else
            break;
    }
    readNewPassword(pass);
    copyStr(librarians[librarianCount].id, id, LEN_ID);
    librarians[librarianCount].passHash = hashPassword(pass);
    librarianCount++;
    if (!saveLibrarians())
    {
        librarianCount--;
        return;
    }
    printf("Librarian account created.\n");
}

/* Returns the librarian index on success, -1 on failure. */
static int librarianLogin(void)
{
    char id[LEN_ID], pass[LEN_PASS];
    printf("\n--- Librarian Login ---\n");
    readText("Librarian ID: ", id, sizeof id);
    readSecret("Password: ", pass, sizeof pass);
    int i = findLibrarian(id);
    if (i < 0 || librarians[i].passHash != hashPassword(pass))
    {
        printf("Invalid ID or password.\n");
        return -1;
    }
    return i;
}

static int studentLogin(void)
{
    char id[LEN_ID], pass[LEN_PASS];
    printf("\n--- Student Login ---\n");
    readText("Student ID: ", id, sizeof id);
    readSecret("Password: ", pass, sizeof pass);
    int i = findStudent(id);
    if (i < 0 || students[i].passHash != hashPassword(pass))
    {
        printf("Invalid ID or password.\n");
        return -1;
    }
    return i;
}

/* =====================================================================
 *  SECTION 9 : PHYSICAL BOOKS
 * ===================================================================== */

static void printBookHeader(void)
{
    printf("\n%-6s %-30s %-20s %-14s %5s %5s\n",
           "ID", "Title", "Author", "Category", "Total", "Avail");
    printf("------------------------------------------------------------------------------------\n");
}

static void printBookRow(const Book *b)
{
    printf("%-6d %-30.30s %-20.20s %-14.14s %5d %5d\n",
           b->id, b->title, b->author, b->category, b->total, b->available);
}

static void displayAllBooks(void)
{
    if (!bookHead)
    {
        printf("\nNo books in the library yet.\n");
        return;
    }
    printBookHeader();
    for (Book *b = bookHead; b; b = b->next)
        printBookRow(b);
}

static void addBook(void)
{
    Book t;
    printf("\n--- Add New Book ---\n");
    while (1)
    {
        t.id = readInt("Book ID: ", 1, MAX_ID);
        if (findBook(t.id))
            printf("  This book ID already exists.\n");
        else
            break;
    }
    readText("Title: ", t.title, sizeof t.title);
    readText("Author: ", t.author, sizeof t.author);
    readText("Category: ", t.category, sizeof t.category);
    t.total = readInt("Number of copies (1-1000): ", 1, 1000);
    t.available = t.total;

    appendBook(newBookNode(t.id, t.title, t.author, t.category, t.total, t.available));
    if (saveBooks())
    {
        logTransaction("BOOK_ADDED", "Book %d (%s), %d copies", t.id, t.title, t.total);
        printf("Book added successfully.\n");
    }
}

static void searchBooks(void)
{
    printf("\n--- Search Books ---\n");
    printf("1. By title\n2. By author\n3. By category\n4. By book ID\n");
    int ch = readInt("Search by: ", 1, 4);
    int found = 0;

    if (ch == 4)
    {
        Book *b = findBook(readInt("Book ID: ", 1, MAX_ID));
        if (b)
        {
            printBookHeader();
            printBookRow(b);
        }
        else
            printf("No book found with that ID.\n");
        return;
    }
    char key[LEN_TEXT];
    readText("Keyword: ", key, sizeof key);
    for (Book *b = bookHead; b; b = b->next)
    {
        const char *field = (ch == 1) ? b->title : (ch == 2) ? b->author
                                                             : b->category;
        if (containsIgnoreCase(field, key))
        {
            if (!found)
                printBookHeader();
            printBookRow(b);
            found++;
        }
    }
    if (!found)
        printf("No matching books found.\n");
    else
        printf("\n%d book(s) found.\n", found);
}

static void updateBook(void)
{
    char buf[LEN_TEXT];
    printf("\n--- Update Book ---\n");
    Book *b = findBook(readInt("Book ID to update: ", 1, MAX_ID));
    if (!b)
    {
        printf("Book not found.\n");
        return;
    }

    printBookHeader();
    printBookRow(b);
    printf("(Press Enter to keep the current value)\n");

    Book t = *b; /* edit a copy first */
    if (readOptional("New title: ", buf, sizeof buf))
        copyStr(t.title, buf, sizeof t.title);
    if (readOptional("New author: ", buf, sizeof buf))
        copyStr(t.author, buf, sizeof t.author);
    if (readOptional("New category: ", buf, sizeof buf))
        copyStr(t.category, buf, sizeof t.category);

    int newTotal;
    if (readIntCore("New total copies: ", 1, 1000, 1, &newTotal))
    {
        int issuedNow = b->total - b->available;
        if (newTotal < issuedNow)
        {
            printf("Cannot reduce below %d: that many copies are currently issued.\n", issuedNow);
            return; /* nothing was changed */
        }
        t.total = newTotal;
        t.available = newTotal - issuedNow;
    }
    *b = t; /* t.next was copied from b */
    if (saveBooks())
    {
        logTransaction("BOOK_UPDATED", "Book %d (%s) updated", b->id, b->title);
        printf("Book updated.\n");
    }
}

static void deleteBook(void)
{
    printf("\n--- Delete Book ---\n");
    int id = readInt("Book ID to delete: ", 1, MAX_ID);
    Book *prev = NULL, *cur = bookHead;
    while (cur && cur->id != id)
    {
        prev = cur;
        cur = cur->next;
    }
    if (!cur)
    {
        printf("Book not found.\n");
        return;
    }
    if (cur->available != cur->total)
    {
        printf("Cannot delete: %d copy(ies) are currently issued.\n", cur->total - cur->available);
        return;
    }
    printBookHeader();
    printBookRow(cur);
    if (!yesNo("Really delete this book?"))
    {
        printf("Cancelled.\n");
        return;
    }

    char title[LEN_TEXT];
    copyStr(title, cur->title, sizeof title);
    if (prev)
        prev->next = cur->next; /* unlink from the list */
    else
        bookHead = cur->next;
    free(cur);
    removeAllWaitingForBook(id);
    if (saveBooks())
    {
        logTransaction("BOOK_DELETED", "Book %d (%s) deleted", id, title);
        printf("Book deleted.\n");
    }
}

/* BUBBLE SORT on the linked list: swap the data of neighbouring nodes. */
static void swapBookData(Book *a, Book *b)
{
    Book *an = a->next, *bn = b->next;
    Book tmp = *a;
    *a = *b;
    *b = tmp;
    a->next = an; /* keep the links in place */
    b->next = bn;
}

static void sortBooks(void)
{
    printf("\n--- Sort Books ---\n1. By title (A-Z)\n2. By book ID (low to high)\n");
    int ch = readInt("Sort by: ", 1, 2);
    if (!bookHead || !bookHead->next)
    {
        displayAllBooks();
        return;
    }

    int swapped;
    do
    {
        swapped = 0;
        for (Book *b = bookHead; b->next; b = b->next)
        {
            int wrongOrder;
            if (ch == 1)
            {
                const char *x = b->title, *y = b->next->title;
                while (*x && *y && tolower((unsigned char)*x) == tolower((unsigned char)*y))
                {
                    x++;
                    y++;
                }
                wrongOrder = tolower((unsigned char)*x) > tolower((unsigned char)*y);
            }
            else
            {
                wrongOrder = b->id > b->next->id;
            }
            if (wrongOrder)
            {
                swapBookData(b, b->next);
                swapped = 1;
            }
        }
    } while (swapped);

    saveBooks();
    logTransaction("BOOKS_SORTED", "Books sorted by %s", ch == 1 ? "title" : "book ID");
    displayAllBooks();
}

/* =====================================================================
 *  SECTION 10 : STUDENTS (librarian view)
 * ===================================================================== */

static void displayStudents(void)
{
    if (studentCount == 0)
    {
        printf("\nNo students registered yet.\n");
        return;
    }
    printf("\n%-15s %-22s %-18s %-4s %s\n", "Student ID", "Name", "Course", "Sem", "E-mail");
    printf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < studentCount; i++) /* passwords are never shown */
        printf("%-15s %-22.22s %-18.18s %-4d %s\n", students[i].id, students[i].name,
               students[i].course, students[i].semester, students[i].email);
}

static void searchStudent(void)
{
    char id[LEN_ID];
    readId("Student ID to search: ", id, sizeof id);
    int i = findStudent(id);
    if (i < 0)
    {
        printf("Student not found.\n");
        return;
    }
    printf("\nID       : %s\nName     : %s\nCourse   : %s\nSemester : %d\nE-mail   : %s\n",
           students[i].id, students[i].name, students[i].course,
           students[i].semester, students[i].email);
    printf("Books currently issued: %d\n", countActiveIssues(id));
}

/* =====================================================================
 *  SECTION 11 : ISSUING AND RETURNING BOOKS
 * ===================================================================== */

/* Fills 'out' with "Due in 5 day(s)" or "OVERDUE 3 day(s), fine Rs. 6". */
static void dueStatus(const Issue *is, char *out, size_t size)
{
    time_t now = time(NULL);
    if (now > is->dueDate)
    {
        long days = (long)((now - is->dueDate + 86399) / 86400); /* round up */
        snprintf(out, size, "OVERDUE %ld day(s), fine Rs. %ld", days, days * FINE_PER_DAY);
    }
    else
    {
        long left = (long)((is->dueDate - now) / 86400);
        snprintf(out, size, "Due in %ld day(s)", left);
    }
}

static void issueBook(void)
{
    char sid[LEN_ID];
    printf("\n--- Issue Book ---\n");
    readId("Student ID: ", sid, sizeof sid);
    if (findStudent(sid) < 0)
    {
        printf("Student not found.\n");
        return;
    }

    Book *b = findBook(readInt("Book ID: ", 1, MAX_ID));
    if (!b)
    {
        printf("Book not found.\n");
        return;
    }

    if (findActiveIssue(sid, b->id) >= 0)
    {
        printf("This student already has a copy of this book.\n");
        return;
    }
    if (countActiveIssues(sid) >= MAX_PER_STUDENT)
    {
        printf("Limit reached: a student can hold only %d books at a time.\n", MAX_PER_STUDENT);
        return;
    }
    if (b->available <= 0)
    {
        printf("No copies available right now.\n");
        if (yesNo("Add the student to the waiting queue?"))
            joinWaitingQueue(sid, b);
        return;
    }
    if (issueCount >= MAX_ISSUES)
    {
        printf("Issue record list is full.\n");
        return;
    }

    Issue *is = &issues[issueCount];
    is->issueId = nextIssueId();
    copyStr(is->studentId, sid, sizeof is->studentId);
    is->bookId = b->id;
    is->issueDate = time(NULL);
    is->dueDate = is->issueDate + (time_t)LOAN_DAYS * 86400;
    is->returned = 0;
    issueCount++;
    b->available--;

    if (!saveIssues())
    { /* undo in memory if saving failed */
        issueCount--;
        b->available++;
        return;
    }
    saveBooks();
    removeWaiting(sid, b->id); /* no longer waiting, if they were */

    char due[16];
    formatDate(is->dueDate, due, sizeof due);
    logTransaction("BOOK_ISSUED", "Issue %d: book %d (%s) to student %s, due %s",
                   is->issueId, b->id, b->title, sid, due);
    printf("Book issued. Issue ID: %d, due date: %s\n", is->issueId, due);
}

static void returnBook(void)
{
    char sid[LEN_ID];
    printf("\n--- Return Book ---\n");
    readId("Student ID: ", sid, sizeof sid);
    int bid = readInt("Book ID: ", 1, MAX_ID);

    int idx = findActiveIssue(sid, bid);
    if (idx < 0)
    {
        printf("No active issue found: this student does not hold that book.\n");
        return;
    }
    Issue *is = &issues[idx];
    Book *b = findBook(bid);

    time_t now = time(NULL);
    long fine = 0;
    if (now > is->dueDate)
    {
        long days = (long)((now - is->dueDate + 86399) / 86400);
        fine = days * FINE_PER_DAY;
        printf("This book is %ld day(s) late. Fine to collect: Rs. %ld\n", days, fine);
    }
    is->returned = 1;
    if (b && b->available < b->total)
        b->available++;

    if (!saveIssues())
    {
        is->returned = 0;
        if (b)
            b->available--;
        return;
    }
    saveBooks();
    logTransaction("BOOK_RETURNED", "Issue %d: book %d returned by %s (fine Rs. %ld)",
                   is->issueId, bid, sid, fine);
    printf("Book returned successfully.\n");

    WaitNode *w = firstWaitingFor(bid); /* QUEUE: who is next? */
    if (w)
        printf("NOTE: student %s is first in the waiting queue for this book. Please inform them.\n", w->studentId);
}

/* Shows active issues. sid == NULL means "everybody" (librarian view). */
static void showIssuedBooks(const char *sid, int withDates)
{
    int found = 0;
    char issued[16], due[16], status[64];
    for (int i = 0; i < issueCount; i++)
    {
        Issue *is = &issues[i];
        if (is->returned)
            continue;
        if (sid && strcmp(is->studentId, sid) != 0)
            continue;
        if (!found)
        {
            printf("\n%-8s ", "IssueID");
            if (!sid)
                printf("%-13s ", "Student"); /* students don't need this column */
            printf("%-7s %-26s", "BookID", "Title");
            if (withDates)
                printf(" %-11s %-11s %s", "Issued", "Due", "Status");
            printf("\n--------------------------------------------------------------------------------------------\n");
        }
        Book *b = findBook(is->bookId);
        printf("%-8d ", is->issueId);
        if (!sid)
            printf("%-13s ", is->studentId);
        printf("%-7d %-26.26s", is->bookId, b ? b->title : "(deleted book)");
        if (withDates)
        {
            formatDate(is->issueDate, issued, sizeof issued);
            formatDate(is->dueDate, due, sizeof due);
            dueStatus(is, status, sizeof status);
            printf(" %-11s %-11s %s", issued, due, status);
        }
        printf("\n");
        found++;
    }
    if (!found)
        printf("\nNo books are currently issued.\n");
}

static void showOverdueBooks(void)
{
    int found = 0;
    time_t now = time(NULL);
    char due[16], status[64];
    for (int i = 0; i < issueCount; i++)
    {
        Issue *is = &issues[i];
        if (is->returned || now <= is->dueDate)
            continue;
        if (!found)
        {
            printf("\n%-8s %-13s %-7s %-24s %-11s %s\n",
                   "IssueID", "Student", "BookID", "Title", "Due", "Status");
            printf("------------------------------------------------------------------------------------\n");
        }
        Book *b = findBook(is->bookId);
        formatDate(is->dueDate, due, sizeof due);
        dueStatus(is, status, sizeof status);
        printf("%-8d %-13s %-7d %-24.24s %-11s %s\n", is->issueId, is->studentId,
               is->bookId, b ? b->title : "(deleted book)", due, status);
        found++;
    }
    if (!found)
        printf("\nGreat! There are no overdue books.\n");
}

/* =====================================================================
 *  SECTION 12 : E-BOOKS
 * ===================================================================== */

static void printEbookHeader(void)
{
    printf("\n%-5s %-28s %-18s %-16s %s\n", "ID", "Title", "Author", "Category", "Status");
    printf("--------------------------------------------------------------------------------\n");
}

static void printEbookRow(const EBook *e)
{
    printf("%-5d %-28.28s %-18.18s %-16.16s %s\n", e->id, e->title, e->author,
           e->category, e->available ? "Available" : "Unavailable");
}

static void listEbooks(int onlyAvailable)
{
    int found = 0;
    for (int i = 0; i < ebookCount; i++)
    {
        if (onlyAvailable && !ebooks[i].available)
            continue;
        if (!found)
            printEbookHeader();
        printEbookRow(&ebooks[i]);
        found++;
    }
    if (!found)
        printf("\nNo e-books to show.\n");
}

static void searchEbooks(void)
{
    printf("\n--- Search E-books ---\n1. By title\n2. By author\n3. By category\n");
    int ch = readInt("Search by: ", 1, 3);
    char key[LEN_TEXT];
    int found = 0;
    readText("Keyword: ", key, sizeof key);
    for (int i = 0; i < ebookCount; i++)
    {
        const char *field = (ch == 1) ? ebooks[i].title : (ch == 2) ? ebooks[i].author
                                                                    : ebooks[i].category;
        if (containsIgnoreCase(field, key))
        {
            if (!found)
                printEbookHeader();
            printEbookRow(&ebooks[i]);
            found++;
        }
    }
    if (!found)
        printf("No matching e-books found.\n");
}

/* Show details, then read the text file page by page. */
static void openEbook(void)
{
    printf("\n--- Open E-book ---\n");
    int idx = findEbook(readInt("E-book ID: ", 1, MAX_ID));
    if (idx < 0)
    {
        printf("E-book not found.\n");
        return;
    }
    EBook *e = &ebooks[idx];

    printf("\nTitle      : %s\nAuthor     : %s\nCategory   : %s\nDescription: %s\n",
           e->title, e->author, e->category, e->description);
    if (!e->available)
    {
        printf("This e-book is currently unavailable.\n");
        return;
    }

    FILE *fp = fopen(e->file, "r");
    if (!fp)
    {
        printf("Error: the file '%s' is missing or cannot be opened.\n"
               "Please inform the librarian.\n",
               e->file);
        return;
    }
    char line[256], cmd[10];
    int lineNo = 0, page = 1;
    printf("\n=============== %s ===============\n", e->title);
    while (fgets(line, sizeof line, fp))
    {
        printf("%s", line);
        if (!strchr(line, '\n'))
            printf("\n");
        if (++lineNo % LINES_PER_PAGE == 0)
        {
            printf("\n--- Page %d | Enter = next page, q = quit ---", page++);
            getLine(cmd, sizeof cmd);
            if (tolower((unsigned char)cmd[0]) == 'q')
            {
                fclose(fp);
                printf("Closed e-book.\n");
                return;
            }
            printf("\n");
        }
    }
    fclose(fp);
    printf("\n--- End of e-book ---\n");
}

static void addEbook(void)
{
    EBook e;
    char name[80]; /* short enough to fit "ebooks/" + name */
    printf("\n--- Add E-book ---\n");
    if (ebookCount >= MAX_EBOOKS)
    {
        printf("E-book list is full.\n");
        return;
    }
    while (1)
    {
        e.id = readInt("E-book ID: ", 1, MAX_ID);
        if (findEbook(e.id) >= 0)
            printf("  This e-book ID already exists.\n");
        else
            break;
    }
    readText("Title: ", e.title, sizeof e.title);
    readText("Author: ", e.author, sizeof e.author);
    readText("Category: ", e.category, sizeof e.category);
    readText("Description: ", e.description, sizeof e.description);
    while (1)
    {
        readText("Text file name inside the ebooks folder (e.g. notes.txt): ", name, sizeof name);
        if (strchr(name, '/') || strchr(name, '\\') || strstr(name, ".."))
        {
            printf("  Only a plain file name is allowed (no folders).\n");
            continue;
        }
        break;
    }
    snprintf(e.file, sizeof e.file, "%s/%s", EBOOK_DIR, name);
    e.available = 1;
    if (!fileExists(e.file))
        printf("Note: %s does not exist yet. Place the file there before students read it.\n", e.file);

    ebooks[ebookCount++] = e;
    if (!saveEbooks())
    {
        ebookCount--;
        return;
    }
    logTransaction("EBOOK_ADDED", "E-book %d (%s) added", e.id, e.title);
    printf("E-book added.\n");
}

static void updateEbook(void)
{
    char buf[LEN_DESC], name[80];
    printf("\n--- Update E-book ---\n");
    int idx = findEbook(readInt("E-book ID to update: ", 1, MAX_ID));
    if (idx < 0)
    {
        printf("E-book not found.\n");
        return;
    }
    EBook t = ebooks[idx];

    printf("(Press Enter to keep the current value)\n");
    if (readOptional("New title: ", buf, sizeof t.title))
        copyStr(t.title, buf, sizeof t.title);
    if (readOptional("New author: ", buf, sizeof t.author))
        copyStr(t.author, buf, sizeof t.author);
    if (readOptional("New category: ", buf, sizeof t.category))
        copyStr(t.category, buf, sizeof t.category);
    if (readOptional("New description: ", buf, sizeof buf))
        copyStr(t.description, buf, sizeof t.description);
    if (readOptional("New file name (inside ebooks folder): ", name, sizeof name))
    {
        if (strchr(name, '/') || strchr(name, '\\') || strstr(name, ".."))
            printf("  Invalid file name, file not changed.\n");
        else
            snprintf(t.file, sizeof t.file, "%s/%s", EBOOK_DIR, name);
    }
    printf("Currently %s.\n", t.available ? "Available" : "Unavailable");
    if (yesNo("Toggle availability?"))
        t.available = !t.available;

    ebooks[idx] = t;
    if (saveEbooks())
    {
        logTransaction("EBOOK_UPDATED", "E-book %d (%s) updated", t.id, t.title);
        printf("E-book updated.\n");
    }
}

static void deleteEbook(void)
{
    printf("\n--- Delete E-book ---\n");
    int idx = findEbook(readInt("E-book ID to delete: ", 1, MAX_ID));
    if (idx < 0)
    {
        printf("E-book not found.\n");
        return;
    }
    printEbookHeader();
    printEbookRow(&ebooks[idx]);
    if (!yesNo("Delete this record? (the text file itself is kept)"))
    {
        printf("Cancelled.\n");
        return;
    }

    EBook removed = ebooks[idx];
    for (int i = idx; i < ebookCount - 1; i++)
        ebooks[i] = ebooks[i + 1];
    ebookCount--;
    if (saveEbooks())
    {
        logTransaction("EBOOK_DELETED", "E-book %d (%s) deleted", removed.id, removed.title);
        printf("E-book record deleted.\n");
    }
}

static void manageEbooks(void)
{
    int ch;
    do
    {
        printf("\n--- E-book Management ---\n");
        printf("1. Add e-book\n2. View all e-books\n3. Search e-books\n");
        printf("4. Update e-book\n5. Delete e-book\n6. Back\n");
        ch = readInt("Enter choice: ", 1, 6);
        switch (ch)
        {
        case 1:
            addEbook();
            break;
        case 2:
            listEbooks(0);
            break;
        case 3:
            searchEbooks();
            break;
        case 4:
            updateEbook();
            break;
        case 5:
            deleteEbook();
            break;
        }
    } while (ch != 6);
}

/* =====================================================================
 *  SECTION 13 : DASHBOARDS
 * ===================================================================== */

static void viewHistory(void)
{
    printf("\n1. Full history (from transactions.txt)\n2. Recent activity (stack)\n");
    if (readInt("Choose: ", 1, 2) == 1)
        showTransactionFile();
    else
        showRecentActivity();
}

static void librarianMenu(int libIndex)
{
    int choice;
    do
    {
        printf("\n=========== LIBRARIAN DASHBOARD (%s) ===========\n", currentUser);
        printf(" 1. Add a new physical book\n");
        printf(" 2. Display all books\n");
        printf(" 3. Search for a book\n");
        printf(" 4. Update book information\n");
        printf(" 5. Delete a book record\n");
        printf(" 6. Display registered students\n");
        printf(" 7. Search for a student by ID\n");
        printf(" 8. Issue a book to a student\n");
        printf(" 9. Return a book\n");
        printf("10. Display currently issued books\n");
        printf("11. Sort books (title or ID)\n");
        printf("12. Manage e-book collection\n");
        printf("13. View transaction history\n");
        printf("14. View overdue books\n");
        printf("15. Change password\n");
        printf("16. Logout\n");
        choice = readInt("Enter choice (1-16): ", 1, 16);

        switch (choice)
        {
        case 1:
            addBook();
            break;
        case 2:
            displayAllBooks();
            break;
        case 3:
            searchBooks();
            break;
        case 4:
            updateBook();
            break;
        case 5:
            deleteBook();
            break;
        case 6:
            displayStudents();
            break;
        case 7:
            searchStudent();
            break;
        case 8:
            issueBook();
            break;
        case 9:
            returnBook();
            break;
        case 10:
            showIssuedBooks(NULL, 1);
            showWaitingList();
            break;
        case 11:
            sortBooks();
            break;
        case 12:
            manageEbooks();
            break;
        case 13:
            viewHistory();
            break;
        case 14:
            showOverdueBooks();
            break;
        case 15:
            if (askChangePassword(&librarians[libIndex].passHash))
            {
                if (saveLibrarians())
                {
                    logTransaction("PASSWORD", "Librarian password changed");
                    printf("Password changed.\n");
                }
            }
            break;
        case 16:
            printf("Logged out.\n");
            break;
        }
    } while (choice != 16);
}

static void showProfile(Student *s)
{
    printf("\n--- My Profile ---\n");
    printf("Student ID : %s\nName       : %s\nCourse     : %s\nSemester   : %d\nE-mail     : %s\n",
           s->id, s->name, s->course, s->semester, s->email);
    printf("Books held : %d of %d allowed\n", countActiveIssues(s->id), MAX_PER_STUDENT);

    if (!yesNo("\nDo you want to update your details?"))
        return;
    char buf[LEN_TEXT];
    Student t = *s;
    printf("(Press Enter to keep the current value. Student ID cannot be changed.)\n");
    if (readOptional("New name: ", buf, sizeof t.name))
        copyStr(t.name, buf, sizeof t.name);
    if (readOptional("New course: ", buf, sizeof t.course))
        copyStr(t.course, buf, sizeof t.course);
    int sem;
    if (readIntCore("New semester (1-12): ", 1, 12, 1, &sem))
        t.semester = sem;
    if (readOptional("New e-mail: ", buf, sizeof buf))
    {
        char *at = strchr(buf, '@');
        if (at && at != buf && strchr(at, '.') && !strchr(buf, ' '))
            copyStr(t.email, buf, sizeof t.email);
        else
            printf("  Invalid e-mail, it was not changed.\n");
    }
    *s = t;
    if (saveStudents())
    {
        logTransaction("PROFILE_UPDATED", "Student %s updated their profile", s->id);
        printf("Profile updated.\n");
    }
}

static void showAvailableCopies(const char *sid)
{
    Book *b = findBook(readInt("Book ID: ", 1, MAX_ID));
    if (!b)
    {
        printf("Book not found.\n");
        return;
    }
    printf("\n'%s' by %s\nTotal copies: %d | Available now: %d\n",
           b->title, b->author, b->total, b->available);
    if (b->available == 0 && yesNo("No copy is free. Join the waiting queue?"))
        joinWaitingQueue(sid, b);
}

static void showRules(void)
{
    printf("\n--- Library Rules and Instructions ---\n");
    printf("1. Each student may borrow up to %d books at a time.\n", MAX_PER_STUDENT);
    printf("2. A book can be kept for %d days from the issue date.\n", LOAN_DAYS);
    printf("3. Late return fine: Rs. %d per day.\n", FINE_PER_DAY);
    printf("4. Take care of the books. Do not write on or damage them.\n");
    printf("5. If a book has no free copy, join the waiting queue.\n");
    printf("6. E-books are for reading only. Please do not copy or share them.\n");
    printf("7. Keep your password private. Librarians never ask for it.\n");
    printf("8. Maintain silence in the library.\n");
}

static void studentMenu(int idx)
{
    Student *me = &students[idx]; /* students only ever see their own record */
    int choice;
    do
    {
        printf("\n=========== STUDENT DASHBOARD (%s) ===========\n", me->name);
        printf(" 1. View my profile\n");
        printf(" 2. Browse all physical books\n");
        printf(" 3. Search for books\n");
        printf(" 4. View available copies of a book\n");
        printf(" 5. View my currently issued books\n");
        printf(" 6. View my issue dates and due dates\n");
        printf(" 7. View available e-books\n");
        printf(" 8. Open / read an e-book\n");
        printf(" 9. Search e-books\n");
        printf("10. Library rules and instructions\n");
        printf("11. Change password\n");
        printf("12. Logout\n");
        choice = readInt("Enter choice (1-12): ", 1, 12);

        switch (choice)
        {
        case 1:
            showProfile(me);
            break;
        case 2:
            displayAllBooks();
            break;
        case 3:
            searchBooks();
            break;
        case 4:
            showAvailableCopies(me->id);
            break;
        case 5:
            showIssuedBooks(me->id, 0);
            break;
        case 6:
            showIssuedBooks(me->id, 1);
            break;
        case 7:
            listEbooks(1);
            break;
        case 8:
            openEbook();
            break;
        case 9:
            searchEbooks();
            break;
        case 10:
            showRules();
            break;
        case 11:
            if (askChangePassword(&me->passHash))
            {
                if (saveStudents())
                {
                    logTransaction("PASSWORD", "Student %s changed password", me->id);
                    printf("Password changed.\n");
                }
            }
            break;
        case 12:
            printf("Logged out.\n");
            break;
        }
    } while (choice != 12);
}

/* =====================================================================
 *  SECTION 14 : MAIN
 * ===================================================================== */

int main(void)
{
    initializeSystem();

    int choice;
    do
    {
        printf("\n==========================================\n");
        printf("        LIBRARY MANAGEMENT SYSTEM\n");
        printf("==========================================\n");
        printf("1. Librarian login\n2. Student login\n");
        printf("3. Register as a student\n4. Register a librarian\n5. Exit\n");
        choice = readInt("Enter choice (1-5): ", 1, 5);

        if (choice == 1)
        {
            int i = librarianLogin();
            if (i >= 0)
            {
                copyStr(currentUser, librarians[i].id, sizeof currentUser);
                printf("Welcome, %s!\n", currentUser);
                librarianMenu(i);
                currentUser[0] = '\0';
            }
        }
        else if (choice == 2)
        {
            int i = studentLogin();
            if (i >= 0)
            {
                copyStr(currentUser, students[i].id, sizeof currentUser);
                printf("Welcome, %s!\n", students[i].name);
                studentMenu(i);
                currentUser[0] = '\0';
            }
        }
        else if (choice == 3)
        {
            registerStudent();
        }
        else if (choice == 4)
        {
            registerLibrarian();
        }
    } while (choice != 5);

    /* Free the memory used by the linked list and the queue. */
    while (bookHead)
    {
        Book *n = bookHead->next;
        free(bookHead);
        bookHead = n;
    }
    while (waitFront)
    {
        WaitNode *n = waitFront->next;
        free(waitFront);
        waitFront = n;
    }

    printf("Goodbye! All data has been saved.\n");
    return 0;
}
