# Set up a Git repository for the FLOWER project

## Current situation

- The full FLOWER Git repository is on the Linux machine (`10.10.1.149`) in
  `~/Projects/flower`. Its working branch is `embedded-client`; it includes the
  C++ client work and the upstream project history.
- The Windows machine has a downloaded copy of FLOWER 1.37.0 with additional
  Windows-side changes for phases A and E, but that copy is not a Git working
  tree.
- The goal is to exchange commits between these two machines on the local
  network. **No repository or commit needs to be pushed to or synchronized
  with GitHub.com.**

## Design

Use a bare Git repository on the Linux machine as the shared LAN remote. Linux
keeps its existing full working repository; Windows clones only the latest
`embedded-client` branch tip using a shallow clone. Git synchronization moves
commits, not uncommitted files, so commit changes on one machine before pulling
them on the other.

The existing Linux `origin` remote may still point to GitHub. The `lan` remote
created below points to the bare repository on Linux. On Windows, the clone's
`origin` will point to that Linux bare repository. In all cases, check the
remote URL before pushing.

## 1. Prepare the Linux machine

Run these commands on Linux. First ensure the current work is committed; do not
continue if `git status` shows changes you still need to preserve.

```bash
cd ~/Projects/flower
git switch embedded-client
git status
```

Create the bare repository and publish the branch to it:

```bash
git init --bare --initial-branch=embedded-client ~/Projects/flower-sync.git
git remote add lan /home/jmazan/Projects/flower-sync.git
git push --set-upstream lan embedded-client
```

The bare repository is the synchronization point; it is not a working
directory. Keep editing the project in `~/Projects/flower`.

Make sure SSH is available on Linux. On Fedora, start the SSH server and, if
`firewalld` is running, allow SSH through the firewall:

```bash
sudo systemctl enable --now sshd
sudo firewall-cmd --permanent --add-service=ssh
sudo firewall-cmd --reload
```

From Windows, verify that the Linux machine is reachable:

```powershell
ssh jmazan@10.10.1.149
```

Accept the host key only if it is the expected Linux machine. Enter the Linux
account password if prompted; SSH key authentication can be set up later to
avoid repeated password prompts.

## 2. Create a shallow working copy on Windows

In PowerShell, clone the branch into a **new** directory. For example:

```powershell
git clone --depth 1 --single-branch --branch embedded-client `
    jmazan@10.10.1.149:/home/jmazan/Projects/flower-sync.git `
    C:\Firma\Projects\JPE\Feng\flower-sync
cd C:\Firma\Projects\JPE\Feng\flower-sync
git remote -v
git status
```

The `origin` shown by `git remote -v` must be the Linux SSH URL above. This
clone contains the current branch tip, not the full project history.

To retain the phase A and E changes from the existing Windows download, copy
the changed and newly created project files from that download into the new
`flower-sync` working copy. Do not copy a `.git` directory. Review what will be
committed, then stage and commit those changes:

```powershell
git status
git add <changed-file-paths>
git commit -m "Add Windows-side phase A and E changes"
git push origin embedded-client
```

Replace `<changed-file-paths>` with the actual file paths (quote paths that
contain spaces). Avoid staging unrelated downloads, generated files, or build
outputs.

## 3. Synchronize changes

Before starting work on a machine, pull commits made on the other machine.
Commit your own changes before pushing them.

On Linux:

```bash
cd ~/Projects/flower
git pull --ff-only
# Edit files, then:
git add <changed-file-paths>
git commit -m "Describe the change"
git push
```

Because the Linux branch tracks `lan` after the setup above, `git pull` and
`git push` use the LAN remote. To be explicit, use
`git pull --ff-only lan embedded-client` and
`git push lan embedded-client`.

On Windows:

```powershell
cd C:\Firma\Projects\JPE\Feng\flower-sync
git pull --ff-only origin embedded-client
# Edit files, then:
git add <changed-file-paths>
git commit -m "Describe the change"
git push origin embedded-client
```

If both machines change the same lines, Git may report a merge conflict during
the pull. Resolve the conflict, commit the resolution, and then push. Do not
use force-push to synchronize.

## Important notes

- The new Windows directory `flower-sync` is the Git working copy to use from
  now on. The original downloaded directory can be kept as a backup until the
  copied changes have been checked.
- The Linux machine and its bare repository retain the full history. The
  shallow Windows clone initially downloads only one commit; older history is
  not available there unless it is explicitly fetched later.
- Never push to the Linux repository's old GitHub `origin`. On Linux,
  `git remote -v` should show `lan` pointing to
  `/home/jmazan/Projects/flower-sync.git`; use `git push lan embedded-client`
  if there is any doubt about which remote will receive a push.
