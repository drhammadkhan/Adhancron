# Notes for Claude

## Git / GitHub operations

- **Always use the GitHub MCP API (`mcp__github__*`) for GitHub write
  operations** — committing/updating files, "pushing", and creating PRs.
  Prefer this over CLI `git push`.
- Rationale: in this remote environment the git proxy that backs CLI
  `git push` / `git fetch` can drop mid-session, causing failures like
  `fatal: could not read Username for 'https://github.com'`. The MCP API
  authenticates independently and keeps working.
- Useful tools: `create_or_update_file` (single file),
  `push_files` (multiple files in one commit), `create_pull_request`.
- When updating an existing file via the API, first get its current blob
  SHA (`git rev-parse origin/<branch>:<path>` or `get_file_contents`) and
  pass it as the `sha` argument, or the update will be rejected.
- After an API commit, the local checkout will diverge from the remote by
  commit SHA (the file content is identical). Reconcile with
  `git fetch origin && git reset --hard origin/main` once CLI git works.
