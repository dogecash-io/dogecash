Contributing to dogd
========================

The dogd project welcomes contributors!

This guide is intended to help developers contribute effectively to dogd.

Communicating with Developers
-----------------------------

To get in contact with dogd developers, you can join the
[eCash Development Telegram group](https://t.me/eCashDevelopment).
The intent of this group is to facilitate development of Bitcoin ABC and other
eCash node implementations. We welcome people who wish to participate.

Acceptable use of this group includes the following:

* Introducing yourself to other eCash developers.
* Getting help with your development environment.
* Discussing how to complete a patch.

It is not for:

* Market discussion
* Non-constructive criticism

dogd Development Philosophy
-------------------------------

dogd aims for fast iteration and continuous integration.

This means that there should be quick turnaround for patches to be proposed,
reviewed, and committed. Changes should not sit in a queue for long.

Here are some tips to help keep the development working as intended. These
are guidelines for the normal and expected development process. Developers
can use their judgement to deviate from these guidelines when they have a
good reason to do so.

- Keep each change small and self-contained.
- Reach out for a 1-on-1 review so things move quickly.
- Land the Diff quickly after it is accepted.
- Don't amend changes after the Diff accepted, new Diff for another fix.
- Review Diffs from other developers as quickly as possible.
- Large changes should be broken into logical chunks that are easy to review,
and keep the code in a functional state.
- Do not mix moving stuff around with changing stuff. Do changes with renames
on their own.
- Sometimes you want to replace one subsystem by another implementation,
in which case it is not possible to do things incrementally. In such cases,
you keep both implementations in the codebase for a while, as described
[here](http://sevangelatos.com/john-carmack-on-parallel-implementations/)
- There are no "development" branches, all Diffs apply to the master
branch, and should always improve it (no regressions).
- Don't break the build, it is important to keep master green as much as possible.
If a Diff is landed, and breaks the build, fix it quickly. If it cannot be fixed
quickly, it should be reverted, and re-applied later when it no longer breaks the build.
- As soon as you see a bug, you fix it. Do not continue on. Fixing the bug becomes the
top priority, more important than completing other tasks.
- Automate as much as possible, and spend time on things only humans can do.

Here are some handy links for development practices aligned with dogd:

- [Developer Notes](doc/developer-notes.md)
- [Statement of Bitcoin ABC Values and Visions](https://archive.md/ulgFI)
- [How to Make Your Code Reviewer Fall in Love with You](https://mtlynch.io/code-review-love/)
- [Large Diffs Are Hurting Your Ability To Ship](https://medium.com/@kurtisnusbaum/large-diffs-are-hurting-your-ability-to-ship-e0b2b41e8acf)
- [Stacked Diffs: Keeping Phabricator Diffs Small](https://medium.com/@kurtisnusbaum/stacked-diffs-keeping-phabricator-diffs-small-d9964f4dcfa6)
- [Parallel Implementations](http://sevangelatos.com/john-carmack-on-parallel-implementations/)
- [The Pragmatic Programmer: From Journeyman to Master](https://www.amazon.com/Pragmatic-Programmer-Journeyman-Master/dp/020161622X)
- [Monorepo: Advantages of monolithic version control](https://danluu.com/monorepo/)
- [Monorepo: Why Google Stores Billions of Lines of Code in a Single Repository](https://www.youtube.com/watch?v=W71BTkUbdqE)
- [The importance of fixing bugs immediately](https://youtu.be/E2MIpi8pIvY?t=16m0s)
- [Slow Deployment Causes Meetings](https://www.facebook.com/notes/kent-beck/slow-deployment-causes-meetings/1055427371156793/)
- [Good Work, Great Work, and Right Work](https://forum.dlang.org/post/q7u6g1$94p$1@digitalmars.com)
- [Accelerate: The Science of Lean Software and DevOps](https://www.amazon.com/Accelerate-Software-Performing-Technology-Organizations/dp/1942788339)
- [Facebook Engineering Process with Kent Beck](https://softwareengineeringdaily.com/2019/08/28/facebook-engineering-process-with-kent-beck/)
- [Trunk Based Development](https://trunkbaseddevelopment.com/)
- [Step-by-step: Programming incrementally](https://www.gamedeveloper.com/programming/step-by-step-programming-incrementally)
- [Semantic Compression](https://caseymuratori.com/blog_0015)
- [Elon Musk's 5-Step Process](https://youtu.be/t705r8ICkRw?t=806)

Contributing to the node software
---------------------------------

During submission of patches, arcanist will automatically run `arc lint` to
enforce dogd code formatting standards, and often suggests changes.
If code formatting tools do not install automatically on your system, you
will have to install the following:

Install all the code formatting tools on Debian Bullseye/Bookworm (11/12) or
Ubuntu 24.04:
```
sudo apt-get install clang-format-16 clang-tidy-16 python3-pip php-codesniffer shellcheck yamllint

# Depending on your distribution policy you might need to pass the
# --break-system-packages to the below pip3 call
pip3 install "black>=24.0" "isort>=5.6.4" "mypy>=0.910" "flynt>=0.78" "flake8>=5" flake8-comprehensions flake8-builtins "djlint>=1.34.1"

echo "export PATH=\"`python3 -m site --user-base`/bin:\$PATH\"" >> ~/.bashrc
source ~/.bashrc
```

If not available in the distribution, `clang-format-16` and `clang-tidy-16` can be
installed from <https://releases.llvm.org/download.html> or <https://apt.llvm.org>.

If you are modifying a shell script, you will need to install the `shellcheck` linter.
A recent version is required and may not be packaged for your distribution.
Standalone binaries are available for download on
[the project's github release page](https://github.com/koalaman/shellcheck/releases).

**Note**: In order for arcanist to detect the `shellcheck` executable, you need to make it available in your `PATH`;
if another version is already installed, make sure the recent one is found first.
Arcanist will tell you what version is expected and what is found when running `arc lint` against a shell script.

If you are running Debian 10, it is also available in the backports repository:
```
sudo apt-get -t buster-backports install shellcheck
```

If you are modifying Rust files, you will need to install a stable rust version,
plus a nightly toolchain called "abc-nightly" for formatting:
```bash
# Install latest stable Rust version
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s
source ~/.cargo/env
rustup install nightly-2023-08-23
rustup component add rustfmt --toolchain nightly-2023-08-23
# Name the nightly toolchain "abc-nightly"
rustup toolchain link abc-nightly "$(rustc +nightly-2023-08-23 --print sysroot)"
```

Contributing to the web projects
--------------------------------

To contribute to web projects, you will need `nodejs` > 20 and `npm` > 10.8.3.
Follow these [installation instructions](https://github.com/nvm-sh/nvm#installing-and-updating)
to install `nodejs` with node version manager.

Then:

```
cd dogd
[sudo] nvm install 20
[sudo] npm install -g npm@latest
[sudo] npm install -g prettier@2.6.0
[sudo] npm install -g eslint@8.3.0
```

Some repositories have a `.nvmrc` file which specifies the version of node expected.
For example, to work in Cashtab,

```
cd dogd/cashtab
nvm use
```

The specified version of nodejs will be installed and used.

To work on the extension, you will need `browserify`

```
[sudo] npm install -g browserify
```

Contributing to Electrum
------------------------

See the dedicated [CONTRIBUTING.md](electrum/CONTRIBUTING.md) document.

Copyright
---------

By contributing to this repository, you agree to license your work under the
MIT license unless specified otherwise in `contrib/debian/copyright` or at
the top of the file itself. Any work contributed where you are not the original
author must contain its license header with the original author(s) and source.

Disclosure Policy
-----------------

See [DISCLOSURE_POLICY](DISCLOSURE_POLICY.md).
