#ifndef INDEX_WRAP_H
#define INDEX_WRAP_H

// Wrap an index by `delta` steps within [0, count), cycling past either end.
// count <= 0 returns 0. Used by list navigation (e.g. the droid-library browser).
inline int wrapIndex(int index, int delta, int count) {
    if (count <= 0) return 0;
    int n = ((index + delta) % count + count) % count;
    return n;
}

#endif // INDEX_WRAP_H
