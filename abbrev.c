// SC is free software distributed under the MIT license

#include "sc.h"

static struct abbrev* abbr_base;

static bool are_abbrevs (void)
{
    return (abbr_base != 0);
}

void add_abbr (char *string)
{
    struct abbrev *a;
    char *p;
    struct abbrev *prev = NULL;
    char *expansion;
    
    if (!string || *string == '\0') {
	if (!are_abbrevs()) {
	    error("No abbreviations defined");
	    return;
	} else {
	    FILE *f;
	    int pid;
	    char px[MAXCMD];
	    char *pager;
	    struct abbrev *nexta;

	    strcpy(px, "| ");
	    if (!(pager = getenv("PAGER")))
		pager = DFLT_PAGER;
	    strcat(px, pager);
	    f = openfile(px, &pid, NULL);
	    if (!f) {
		error("Can't open pipe to %s", pager);
		return;
	    }
	    fprintf(f, "\n%-15s %s\n","Abbreviation","Expanded");
	    if (!brokenpipe) fprintf(f, "%-15s %s\n", "------------",
		    "--------");

	    for (a = nexta = abbr_base; nexta; a = nexta, nexta = a->a_next)
		;
	    while (a) {
		fprintf(f, "%-15s %s\n", a->abbr, a->exp);
		if (brokenpipe) return;
		a = a->a_prev;
	    }
	    closefile(f, pid, 0);
	    return;
	}
    }

    if ((expansion = strchr(string, ' ')))
	*expansion++ = '\0';

    if (isalpha(*string) || isdigit(*string) || *string == '_') {
	for (p = string; *p; p++)
	    if (!(isalpha(*p) || isdigit(*p) || *p == '_')) {
		error("Invalid abbreviation: %s", string);
		scxfree(string);
		return;
	    }
    } else {
	for (p = string; *p; p++)
	    if ((isalpha(*p) || isdigit(*p) || *p == '_') && *(p+1)) {
		error("Invalid abbreviation: %s", string);
		scxfree(string);
		return;
	    }
    }
    
    if (!expansion) {
	if ((a = find_abbr(string, strlen(string), &prev))) {
	    error("abbrev \"%s %s\"", a->abbr, a->exp);
	    return;
	} else {
	    error("abreviation \"%s\" doesn't exist", string);
	    return;
	}
    }
 
    if (find_abbr(string, strlen(string), &prev))
	del_abbr(string);

    a = (struct abbrev *)scxmalloc((unsigned)sizeof(struct abbrev));
    a->abbr = string;
    a->exp = expansion;

    if (prev) {
	a->a_next = prev->a_next;
	a->a_prev = prev;
	prev->a_next = a;
	if (a->a_next)
	    a->a_next->a_prev = a;
    } else {
	a->a_next = abbr_base;
	a->a_prev = NULL;
	if (abbr_base)
	    abbr_base->a_prev = a;
	abbr_base = a;
    }
}

void del_abbr (char *abbrev)
{
    struct abbrev *a;
    struct abbrev **prev = NULL;

    if (!(a = find_abbr(abbrev, strlen(abbrev), prev))) 
	return;

    if (a->a_next)
        a->a_next->a_prev = a->a_prev;
    if (a->a_prev)
        a->a_prev->a_next = a->a_next;
    else
	abbr_base = a->a_next;
    scxfree((char *)(a->abbr));
    scxfree((char *)a);
}

struct abbrev* find_abbr (char* abbrev, int len, struct abbrev** prev)
{
    struct abbrev *a;
    int cmp;
    int exact = TRUE;
    
    if (len < 0) {
	exact = FALSE;
	len = -len;
    }

    for (a = abbr_base; a; a = a->a_next) {
	if ((cmp = strncmp(abbrev, a->abbr, len)) > 0)
	    return (NULL);
	*prev = a;
	if (cmp == 0)
	    if (!exact || strlen(a->abbr) == (unsigned)len)
		return (a);
    }
    return NULL;
}
