// Reintroduced-warning fixture. This file MUST fail to compile under the
// centralized warnings-as-errors gate -- CTest asserts the failure via
// WILL_FAIL. It stores a private data member that is never read, reproducing the
// exact clang-only diagnostic (-Wunused-private-field) that once reached a
// downstream consumer non-fatally: the gate is proven to bite on that class of
// warning rather than merely printing it. Registered under clang only, since GCC
// has no equivalent diagnostic and would compile this cleanly.

namespace {

class stored_but_unread
{
public:
    explicit stored_but_unread(int seed)
        : m_seed(seed)
    {
    }

private:
    int m_seed;
};

}

int main()
{
    stored_but_unread instance(0);
    (void)instance;
    return 0;
}
