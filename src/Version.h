/*
P i https://stackoverflow.com/a/54067471
*/
#include <string>
#include <iostream>

// Method to compare two version strings
//   v1 <  v2  -> -1
//   v1 == v2  ->  0
//   v1 >  v2  -> +1
int version_compare(std::string v1, std::string v2)
{
    size_t i = 1, j = 1; // start version string with v...
    while (i < v1.length() || j < v2.length())
    {
        int acc1 = 0, acc2 = 0;

        while (i < v1.length() && v1[i] != '.')
        {
            acc1 = acc1 * 10 + (v1[i] - '0');
            i++;
        }
        while (j < v2.length() && v2[j] != '.')
        {
            acc2 = acc2 * 10 + (v2[j] - '0');
            j++;
        }

        if (acc1 < acc2)
            return -1;
        if (acc1 > acc2)
            return +1;

        ++i;
        ++j;
    }
    return 0;
}

struct Version
{
    std::string version_string;
    Version(std::string v) : version_string(v)
    {
    }
};

bool operator<(Version u, Version v) { return version_compare(u.version_string, v.version_string) == -1; }
bool operator>(Version u, Version v) { return version_compare(u.version_string, v.version_string) == +1; }
bool operator<=(Version u, Version v) { return version_compare(u.version_string, v.version_string) != +1; }
bool operator>=(Version u, Version v) { return version_compare(u.version_string, v.version_string) != -1; }
bool operator==(Version u, Version v) { return version_compare(u.version_string, v.version_string) == 0; }