#pragma once

/*
Goals with new Analyser:
 - Use AST with Semantic Information
 - Remove unnecessary Stages/Data-Representation from Process (RC-Code, Modtree)
 - Simpler Dependency Handling
 - Simpler/More powerful Analysis

Problems with current Approach:
 - using Statements
 - Polymorphic Functions/Templates (Analysis + Instanciation)
 - includes
 - Function_Cluster_Compile
 - Struct_Reachable_Resolve
 - Bake Expressions

My previous approaches were:
 * Stop and Go with Statement Granularity:
    This meant that there was a lot of downtime, since Expressions could potentially
    have lots of Dependencies, resulting in a lot of Switching.
    Also Global Symbols in a local Context (Comptime definitions inside a function, e.g. inner functions)
    were almost impossible to do
 * Analyse all Dependencies first:
    By doing a prepass almost all Symbols can be discovered (Except Cond. Compilation), 
    and big Workloads can be analysed in one Go without stopping. The problems here is that
    analysing all dependencies before knowing Context information can
    be difficult or annoying to deal with (Struct Reachable/Function Runnable, Bake-Expressions)
    
Further Problems:
 - Using Statement in Global Context influences Symbol Resolution
 - Using in Local Context requires expression-information
    x := Game~get_top_player()
    using x~*
    a += health // x.health
    using Datastructures~Hashtable

Potential Solution: Stop and Go with Fiber Granularity + Previous Discovery Stage

Problem: Symbol Discovery/Symbol Tables
*/

namespace AST {
    struct Module;
}

namespace Semantic_Analysis
{
    void initialize();
    void destroy();
    void analyse_ast(AST::Module* root);
}
