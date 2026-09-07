### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
name: fork-join-loop
version: "0.1.0"
agent_loop: fork_join
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: [/main/fork_tasks]
  - id: fork_tasks
    type: fork
    branches:
      - path: /main/task_a
      - path: /main/task_b
      - path: /main/task_c
    next: [/main/join_results]
  - id: task_a
    type: tool_call
    tool: loop/process_task
    args:
      task_id: "a"
      input: "{{user_input}}"
    output_keys: [result_a]
    next: [/main/end]
  - id: task_b
    type: tool_call
    tool: loop/process_task
    args:
      task_id: "b"
      input: "{{user_input}}"
    output_keys: [result_b]
    next: [/main/end]
  - id: task_c
    type: tool_call
    tool: loop/process_task
    args:
      task_id: "c"
      input: "{{user_input}}"
    output_keys: [result_c]
    next: [/main/end]
  - id: join_results
    type: join
    wait_for: [/main/task_a, /main/task_b, /main/task_c]
    next: [/main/synthesize]
  - id: synthesize
    type: llm_call
    prompt_template: |
      Synthesize the following parallel task results:

      Task A: {{result_a}}
      Task B: {{result_b}}
      Task C: {{result_c}}
    output_keys: [synthesis]
    next: [/main/end]
  - id: end
    type: end
    termination_mode: hard
# --- END AgenticDSL ---
```
