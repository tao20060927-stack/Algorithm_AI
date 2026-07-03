#!/bin/bash
cat << 'EOF'
# TAB Presentation Structure (32 slides)

## Core (Slides 1-13) - Always include

1. **Title** - Date, Version, Status, Author, Decision/Information
2. **Context** - TDA/TAB history, previous reviews
3. **Authors/Contributors** - Name, Role, Date Approved
4. **Approvers** - Name, Role, Date Approved
5. **Attendees** - Name, Role, Date Attended
6. **Business Context** - 5-min intro: What? Why?
7. **Terminology** - Define project/domain terms
8. **Scope** - In scope, out of scope, deferred decisions
9. **Scale & Scope** - Repos, users, data volume counts
10. **Solution Overview (DRA)** - Context diagram
11. **Solution Overview (Detail)** - Integration points
12. **Solution Overview (Design)** - What's new/changed/removed
13. **Conclusions & Next Steps** - Decision request

## Socialisation (Slides 14-32) - Reference material

14. Section divider
15. **Data Sources** - Mock vs real, classification
16. **Key Architectural Decisions** - ID, Date, Decision, Status
17. **Vendor Considerations** - Portability statement
18. **Functional Requirements** - ID, Requirement, Priority
19. **Non-Functional Requirements** - ID, Requirement, Target
20. **Success Criteria** - With measurement methods
21. **Data View** - Data model, interfaces, flows
22. **Application View** - Components: new, existing, removed
23. **Technology View** - Tech mapped to applications
24. **Security View** - CIA, controls
25. **Systems Management** - Monitoring, logging, alerting
26. **Technical Debt** - Introduced, resolved, remaining
27. **RAIDS: Assumptions** - ID, Assumption, Actions
28. **RAIDS: Risks** - With phase (PoC/Prod/Both)
29. **RAIDS: Issues** - ID, Issue, Actions, Status
30. **RAIDS: Dependencies** - ID, Dependency, Actions, Status
31. **Options Comparison** - Pros, cons, cost, recommendation
32. **Conclusions (Detailed)** - Evidence refs, decision, next steps
EOF
