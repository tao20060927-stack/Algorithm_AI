---
name: arch-docs
description: Generate architecture documentation and TAB review packs. Use for "prepare for TAB", "create review pack", "document architecture", "generate HLD/LLD", or technical governance preparation.
---

# Architecture Documentation Generator

Generate documentation for Technical Assurance Board and peer engineering reviews.

## Output Files

Create in `docs/review-pack/{DATE}/`:

| File | Purpose |
|------|---------|
| `evidence-map.md` | Claims mapped to code evidence |
| `hld.md` | High-Level Design (C4 L1-2) |
| `lld.md` | Low-Level Design (C4 L3) |
| `peer-review.md` | Engineering review with gaps |
| `risk-register.md` | Risks with mitigations |
| `decision-log.md` | Decisions made and pending |
| `tab-pack.md` | Board narrative document |
| `tab-slides.md` | Slide content |

## Process

### Phase 1: Discovery
1. Read business context documents
2. Explore codebase structure
3. Create repository index (directories, entry points, how to run/test)

### Phase 2: Flow Trace
Pick ONE representative journey and trace: Request -> routing -> validation -> persistence -> external calls -> response. Document with exact file paths.

### Phase 3: Analysis
Run: `scripts/load-lenses.sh` for TAB concerns, `scripts/load-checklists.sh` for assessment checklists. Complete analysis against each.

### Phase 4: Generate Documents
Run `scripts/load-template.sh {doc-type}` to get the template for each document. Generate in order:
1. evidence-map (establishes claims)
2. hld -> lld (architecture)
3. peer-review (assessment)
4. risk-register -> decision-log
5. tab-pack -> tab-slides

### Phase 5: Review
- Cross-reference claims against evidence-map
- Mark unproven claims `[UNPROVEN]`
- Audit placeholders before finalising

## Principles

- **Evidence-led**: Every claim needs a file path or marked `[UNPROVEN]`
- **Constructively critical**: Surface risks, frame as "approve with conditions"
- **Plain English**: UK public sector tone, no marketing language
- **Scope clarity**: Distinguish current phase from future phases
- **Measurable outcomes**: Success criteria need measurement methods

## Placeholders

| Marker | Meaning |
|--------|---------|
| `[TBC]` | To be confirmed |
| `[UNPROVEN]` | Claim without evidence |
| `[UNDEFINED-TERM]` | Term needs glossary definition |
| `[MEASUREMENT-TBC]` | Needs measurement method |
| `[EXAMPLE-NEEDED]` | Abstract claim needs example |
| `{OWNER}` | Assign responsible person |
| `{DATE}` | Replace with actual date |

## On-Demand References

Load only what you need:
- `scripts/load-lenses.sh` - TAB concern framework
- `scripts/load-checklists.sh` - Security, operability, testing
- `scripts/load-template.sh <type>` - Document templates (hld|lld|risk|decision|tab-pack|tab-slides|evidence|peer-review)
- `scripts/load-slides.sh` - 32-slide TAB presentation structure
