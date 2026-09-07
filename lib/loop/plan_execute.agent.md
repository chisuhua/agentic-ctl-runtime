### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
name: plan-execute-loop
version: "0.1.0"
agent_loop: plan_execute
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: [/main/plan]
  - id: plan
    type: llm_call
    prompt_template: |
      You are a planning agent. Given the user's request, produce a step-by-step plan.

      User request: {{user_input}}
      Context: {{context}}
    output_keys: [plan_response]
    next: [/main/execute]
  - id: execute
    type: tool_call
    tool: loop/execute_plan
    args:
      plan: "{{plan_response}}"
    output_keys: [execution_result]
    next: [/main/verify]
  - id: verify
    type: llm_call
    prompt_template: |
      Verify the execution result against the original plan.

      Plan: {{plan_response}}
      Result: {{execution_result}}
    output_keys: [verification]
    next: [/main/end]
  - id: end
    type: end
    termination_mode: hard
# --- END AgenticDSL ---
```
