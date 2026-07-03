#!/bin/bash
# Load specific document template
# Usage: load-template.sh <type>
# Types: hld, lld, risk, decision, tab-pack, tab-slides, evidence, peer-review

case "$1" in
  evidence|evidence-map)
    cat << 'EOF'
# Evidence Map Template

```markdown
# Evidence Map

**Project:** {PROJECT}  |  **Date:** {DATE}

## {Category} Claims

| Claim | Evidence | Status |
|-------|----------|--------|
| "{Claim}" | `path/file.ts:line` | PROVEN/UNPROVEN |

## Open Questions
1. Question needing clarification

## Remaining Placeholders
- List [TBC], [UNPROVEN] items needing resolution
```
EOF
    ;;
  hld)
    cat << 'EOF'
# HLD Template

```markdown
# High-Level Design (HLD)

**Project:** {PROJECT}  |  **Version:** 0.1  |  **Date:** {DATE}

## 1. Introduction
Purpose | Scope | Audience

## 2. System Context (C4 Level 1)
[Mermaid C4Context diagram]

## 3. Container Diagram (C4 Level 2)
[Mermaid C4Container diagram]

## 4. Key Components
| Component | Location | Responsibility |

## 5. Data Flow
[Mermaid sequence diagram]

## 6. Integration Points
| System | Protocol | Auth | Status |

## 7. Non-Functional Requirements
Security | Availability | Performance

## 8. Technology Stack
| Layer | Technology | Version |

## 9. Vendor Considerations
| Component | Vendor | Alternatives | Migration Complexity |
Portability Statement: [How easily switch providers?]

## 10. Deployment Model

## 11. Security Architecture

## 12. Glossary
| Term | Definition |
```
EOF
    ;;
  lld)
    cat << 'EOF'
# LLD Template

```markdown
# Low-Level Design (LLD)

**Project:** {PROJECT}  |  **Version:** 0.1  |  **Date:** {DATE}

## 1. Component Details (C4 Level 3)
For each key component:
- Purpose
- Dependencies
- Key classes/functions with file paths
- [Mermaid C4Component diagram]

## 2. Data Models
- Entity relationships
- Database schema
- API contracts

## 3. Sequence Diagrams
Key flows with exact method calls

## 4. Error Handling
| Error Type | Handler | Recovery |

## 5. Configuration
| Setting | Purpose | Default |
```
EOF
    ;;
  risk|risk-register)
    cat << 'EOF'
# Risk Register Template

```markdown
# Risk Register

**Project:** {PROJECT}  |  **Date:** {DATE}

## Summary
| ID | Title | Phase | L | I | Score | Status |

**Phase:** PoC / Production / Both

## RISK-{ID}: {Title}
- **Category:** Security / Technical / Operational / Commercial
- **Applies To:** PoC / Production / Both
- **Description:** What could go wrong
- **Likelihood:** H / M / L
- **Impact:** H / M / L
- **Mitigation:** Actions to reduce
- **Owner:** {OWNER}
- **Status:** Open / Mitigating / Accepted / Closed
- **TAB Ask:** Approval/condition needed
```
EOF
    ;;
  decision|decision-log)
    cat << 'EOF'
# Decision Log Template

```markdown
# Decision Log

**Project:** {PROJECT}  |  **Date:** {DATE}

## Summary
| ID | Decision | Status | Date |

## DEC-{ID}: {Title}
- **Date:** {DATE}
- **Status:** Decided / Pending
- **Context:** Why needed
- **Options:**
  1. Option A - pros, cons
  2. Option B - pros, cons
- **Decision:** Chosen option
- **Rationale:** Why
- **Consequences:** Enables/prevents
- **Evidence:** `path/to/code`
```
EOF
    ;;
  tab-pack)
    cat << 'EOF'
# TAB Pack Template

```markdown
# Technical Assurance Board Pack

**Project:** {PROJECT}  |  **Date:** {DATE}  |  **Version:** 0.1  |  **Status:** Draft

## Document Control
| Version | Date | Author | Changes |

## Executive Summary
[Brief: what, status, ask]

## Terminology
| Term | Definition |

## Scope
**In Scope:** | **Out of Scope:** | **Deferred:**

## Problem Statement
Current Situation | Business Impact

## Objectives & Success Criteria
| Criterion | Target | Measurement | Measured By |

## Scale
| Dimension | Count | Notes |

## Data Sources
| Source | Type (Mock/Real) | Classification | Notes |

## Solution Overview
What We're Building | Architecture Summary

## Vendor Considerations
| Component | Vendor | Alternatives | Migration Complexity |
Portability Statement:

## Technology Stack
| Component | Technology | Rationale |

## Options Considered
Option A vs B - Pros, Cons, Recommendation

## Risks & Mitigations
| Risk | Phase | L | I | Mitigation | TAB Ask |

## Security & Compliance
| Control | Status | Evidence |

## Timeline
| Milestone | Date | Status |

## Conclusions & Recommendations
TAB Decision: Approve with conditions / Unconditional / Defer / Reject

## Remaining Placeholders
[List all [TBC], {OWNER}, etc.]
```
EOF
    ;;
  tab-slides)
    cat << 'EOF'
# TAB Slides Template

```markdown
# TAB Slides

**Project:** {PROJECT}  |  **Date:** {DATE}

## Slide {N}: {Title}

**Key message:** One sentence

**Content:**
- Bullet 1
- Bullet 2

**Notes:** Presenter context

**Evidence:** `path/file` or data source
```

Use `scripts/load-slides.sh` for full 32-slide structure.
EOF
    ;;
  peer-review)
    cat << 'EOF'
# Peer Review Template

```markdown
# Peer Review

**Project:** {PROJECT}  |  **Date:** {DATE}  |  **Reviewer:** {REVIEWER}

## Summary
Overall assessment | Key strengths | Key concerns

## Architecture Assessment
| Aspect | Rating | Notes |
|--------|--------|-------|
| Clarity | Good/Fair/Poor | |
| Modularity | | |
| Testability | | |
| Security | | |

## Code Quality
| Area | Evidence | Assessment |
|------|----------|------------|
| Structure | `path` | |
| Error handling | | |
| Logging | | |

## Gaps Identified
| ID | Gap | Severity | Recommendation |

## Recommendations
1. Must-fix before production
2. Should-fix
3. Nice-to-have
```
EOF
    ;;
  *)
    echo "Usage: load-template.sh <type>"
    echo "Types: hld, lld, risk, decision, tab-pack, tab-slides, evidence, peer-review"
    exit 1
    ;;
esac
