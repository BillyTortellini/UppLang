# Design Document

## Introduction
In this Document I try to write down my Knowledge, Experience and Thoughts about how I think Programming should and shouldn't be done.
Since I am currently developing/designing a new programming language + IDE, I need some Principles and Guidelines
upon which Design-Decision are made, and in this Document I would like to write these down. Furthermore, this document
should also include new, untested Ideas and Knowledge that I have gained during the development process. The process of writing
this document should also show me which areas I need to further research.

## Motivation
My motivation for creating a new programming language has multiple Aspects. Since I always was interested in Computers, and
my educational path up till revolved around Informatics, it is quite safe to assume that I will spend a huge part of my Life
programming. This alone would be enough reason to try to make the the current programming-world a better place. 
But I'm also personally invested in continually becoming better in my Trade, and ... 

I could write a lot more here, and I probably should at some point, e.g. 
 * doing this for my own personal advancement (E.g. how learning about compilers helps you while programming)
 * Problems with other languages (Has its own subtopic
 * Are programmers limited by their own abilities or are they limited by current tools?
 * Interesting Design-Challenges
 * Good practice for programming

## Why I like programming
 * Solving interesting problems
 * Some similarities to solving puzzles
 * Computers are fast, so programming should be too

## References and Attributions
 * Jonathan Blow talks
 * Casey Muratori (Talks, Blog, ...), handmade-hero
 * Mike Acton, Data oriented Programming
 * Ryan Fleury & Allen Webster, Dion Systems
 * Abner coimbre, Handmade Community
 * Matt Might, Solving the Halting Problem
 * Eskil Steenberg, "How I program C"
 * John Carmack
 * Rob Pike, Go
 * ... (Generally lots of the talks that I have watched on youtube should probably be mentioned here)

## The Goal of computers
Industrial Revolution automated 'Human Strength'/Power of muscle.
Industry 1.0 to 2.0 were about 'automating' Strength, and increasing manufacturing speed (Assembly lines, mass production)

Industry 3.0, IT and computers, were supposed to automate 'thinking', by using computer for such tasks.
This never really happend, but instead of that we use Computers as Tools for thinking, similar to how
most people cannot calculate 'simple' maths in their head (e.g. 923414 * 3923.32 / 12.4), but are able to solve
this with a pen and paper. Then theres also the internet and knowledge sharing, simulations ... (Refernenc eto Jonathan Blows human condition)

### What computer do/why programming is hard
Content: Why Low-Code and 4th Generation Programming languages don't work, limits of libraries and Code-Reuse

Computers are used because they solve tasks __exactly__ and __fast__. Otherwise
these tasks could be done by humans. Humans can't do these tasks fast, because
we would need to write things down, e.g. our brain is not build to solve these mathematical tasks.
Humans also have a lot of trouble to do things __exactly__, because we may forget/skip required steps, don't think
about special conditions and so on. This is why we use Computers, but first we need to program them.
During coding, most of our problems come from the second Goal of Computer, their exactness/correctness.
Because we humans usually never think exactly about how we do things (e.g. get some coffee), we
have a hard time explaining what it exactly means to do something. There is a similarity to
children always asking 'why', where you stop them at some point because you cannot explain certain things.
In programming, you need to answer the computers questions of 'how' over and over again.
As an example, you cannot write a programm by typing 'Calculate the sum of all my expenses', 
you need to specify exactly how to calculate the sum (adding 32bit floats in a loop), how and
where the expenses come from (input from file, gui, network, command line, ...) and so on, until
you have a complete, __precise__ specification of what the task is. The Code we write is essentially this specification, on how 
exactly to do a given task to a achieve a goal.



## Current Problems in Programming
 * Some introduction about how Programming isn't around for too long, and nobody really knows how to program, which
    is somehow displayed by all the different ways of how programming is done nowadays. By that I mean that
    we know how to do certain things (Like running a Webserver for thousands of Users), but we don't know what the best
    way of doing this is (See the abundance of JavaScript Frameworks, and the changes of Technologies in the last couple years).
 * Problems with the power of turing completeness, Programming-Tasks can be solved in an infinite number of ways (Which is a problem).
 * __Big 2 Factors__ of Software-Quality (Performance, Correctness) + __1 Factor of Development__ (Development Time) and the
    derived factors that people seem to care about (Maintainability, readability, simplicity, scalability, Future-Proof, Reusable, ...),
    Joy of Programming
 * problems with currently used/old languages (C/C++, Java)
 * Problems of object-oriented-programming and functional programming, other programming paradigms (Note: Data-Oriented Programming)
 * Problems with going up the ladder of abstraction
 * Software-Development, Software-Engineering, TDD, Development-Processes, Agile, ... (See talk "Scams that derail Programming")
 * new languages currently in development (zig, jai, odin, nim, rust, go, D, carbon)
 * Why systems programming languages will always be necessary
 * Advantages/Disadvantages of High-Level languages vs System languages (Garbage collection, null, segfault, References)
 * How to achieve peak performance (Memory, Cache, Parallel Programming, Interpreters, Dynamic vs Static Type-Systems, no static-type checking), also note
    some idiotic ways on how people sometimes compare languages. 
 * Hardware not getting faster, multicore is the future?
 * Visual programming languages, other approaches to writing Code
 * Programming Language Generations, the failure of 4th generation Programming languages, myth of LowCode/NoCode
 * Combining low and High-level languages (Python + NumPy, others...)

## History of programming language features
 * First Computers, first bug
 * Programming with punch cards and other things
 * Advent of modern programming, Machine Code
 * Assembly, the human readable form of code
 * Names/Symbols in assembly (Instead of numbers)
 * Goto, functions + stack
 * Structured programming, one of the first cases of documenting progammer 'intent' in code, without practical meaning for compiler
 * Object oriented programming, Java, Objects on the heap, referencability

## Exploration-Based Design
Theoretically any problem can be solved by applying Richard-Feynman's Problem-Solving Algorithm:
 1. Write down the problem
 2. Think very hard
 3. Write down the answer
Sadly in practice most difficult Problems cannot be solved by applying this Approach, but it is interesting
to think about why this doesn't work and how we still find ways to help us solve problems.

Usually different Disciplies use different techniques to find Solutions, but I would
like to think about the techniques that we have in Programming to do so.

A general Process that I use when trying to find Solutions is the following one:
1. At first its necessary to figure out what the Problems are that the System should solve,
    and why these Problems exist in the first place.
1. Then I imagine how these Problems would be solved in in a perfect, idealized World. The Solutions that come up here can and should be unrealistic, 
    like imagining how a a general purpose AI could generate Programs automatically, or how we could use neural Implants to 
    communicate directly with a computer instead of using a mouse and keybord. Lots of different ideas should be considered here.
1. After that, I look at why current Things aren't done in this perfect, idealized way. For example, we currently don't use
    general AI to generate Programs because general AI doesn't exist (yet). We don't use planes and helicoptors to travel everywhere because
    it isn't realisticly achievable with our resources. We don't write perfectly optimized Code everywhere during coding because
    it takes more time and doesn't pay off in 99% of all cases. The purpose of this stage is to find all restrictions and
    limitations that need to be considered when designing a new System. After finding these Restrictions, we should go back to 
    Step one and try to get Solutions which achieve the Goals with the new Restrictions in mind. This is usually a cyclic process
    between coming up with new solutions, finding restrictions why they cannot work and then coming up with solutions that solve these restrictions. 

The result of the previous two stages is a list of restrictions that solutions need to have and a list of possible solutions that fulfill
    these restrictions. This process should also be guided by using already existing Ideas and Solutions, e.g. looking at Literature and 
currently used programs. 
1. Literature Reserach: 
    I look at how things currently are done by other programming languages/companies, and try to figure out why they are
    done this way/why these products aren't like the ones in a perfect world. I feel like a lot of the time things are done
    in a specific way without even having good reasons for them. Especially with old systems, like old programming languages,
    the designs are bad because of their development history and because some form of compatabilty needs to be kept. This
    is also true for modern Systems that are constantly updated, a prime example would be HTML/CSS. At least I don't have another 
    explanation why it is incredibly hard and unintuitive to center elements on a website. 

## How Programming should be done
 * Design process (Goals, etc...)
 * Decision making process (Decisions should be made upon facts, future decisions can be decided based on previous ones)
 * Exploration based Design (Casey Murator/Jonathan Blow)
 * Technical Dept
 * Compression Based programming (Casey muratori)
 * Deleting Code
 * Write Code as you need it
 * When to use a simple solution (With design drawbacks), when to use the full solution
 * Why Future Proofing, Abstractions e.g. aren't useful (Mike Acton)
 * Difference between programming a library and programming a Product, differentiate between
    product code and reusable code. 

## Why Handmade, "Reinventing the Wheel"
 * There are no wheels
 * Extern libraries vs writing something internally
 * At first you want speed, at the end you want control (Maybe something about game engines)

## Programming-Style
 * What programming style is/isn't, why its important.
 * Casey Muratori 'Where bad programming comes from'

### Code-Layout

### Syntax

## Upp-Language features
Pillars of the language
 * Compile-Time Code-Execution
 * 'Interactive' Static Analysis
 * Code Visualization, integrated IDE


# Uncategorized Throughts

## Idea, Implementation and Interaction (Man-month)

## When to make Decisions

*NOTE*: I'm also not quite sure if I really am talking about decisions here, or if I mean the
Implementation-Order of sub-modules of a program.

Both during Design and Programming, alternative Solutions to Problems need
to be considered and a Decisions then has to be made to pick one of the possible Solutions.

There is an interesting Dilemma about the time at which Decisions should be made:
 * On one hand, Decisions should be made as late as possible, because then you have the
   greatest amount of Knowledge about the Problem
 * On the other hand, making a Decision early allows you to base future Decisions on
   the results of this Decisions. 

The old, classical Software-Management Models (like Waterfall, V-Modell, ...) all suffer
from the problem of making Decisions early, because they rely on having a long Design-Phase before Programming, and so the Decisions made during
the Design cannot be based on any experience made from programming.

So is it always the case that Decisions should be postponed as long as possible?
Sadly the problem isn't as simple, because at some point Decisions have to be made in
order to make Progress/gather Experience. In a perfect World, all Decisions would be made based
upon Facts which can be discovered by just looking long enough, and are known to always be true. In practice, Decisions
depend on Facts of the Problem AND on other made Decisions. In a sense, after making a decision,
you add more Information about your Solution to the project, and this information can then be
used to guide other Decisions.

These options then should be weighted by their advantages and disadvantages, and the best option should be choosen. 
    This is in general very difficult, because 
     * Determining all the advantages and disadvantages may not be possible because of limited Knowledge about the Problem
     * Comparing advantages/disadvantage between solutions may not be possible
    It would be best Practice to prototype all possible solutions and then make Decisions with more advanced Knowledge,
    but this is practically unrealistic as implementing and testing lots of prototypes would be required.

It would be best Practice to reevaluated previous Decisions once new knowledge is available.

Also of note is that Decisions can be classified by multiple factors, for example their complexity or their
importance for future decisions. Hard decisions should generally be done later, since then more information is
available. It is also often useful to implement a temporary solution, so that further parts of the program can be developed.
If this is the case, it is important that future Decisions aren't based on the temporary solution.

To give some examples for common decisions in programming:
 * Which programming language is used
 * How to implement a specific feature
 * Which algorithm to use, is a simple implementation enough or is a more sophisticated approach needed
 * Which datastructure to use
 * Should a previous solution be written to be more general, but therefore slower, or should different versions exist
 * Should an external libray be used, or should a new solution be manifactured
 * In which order should the sub-modules of a program be implemented
 * Should the different sub-modules be implemented depth-first or breath-first

## Important/Popular Systems become resistant to change
When a System that is currently beeing used has to be updated because it can be improved
by a new feature or a new Discovery is made, it seems to make sense to implement this improvement as
soon as possible, especially when a lot of people are using it. In reality, there is the
tradeoff that new Features may introduce new Bugs, Instabilities, Incompabilities and Inconsistencies.
This is true for both Systems that are used by lots of people, and systems that are used by a few.

The interesting Discovery here is that it seems like the probability of getting important and
usefull Updates decreases as the number of users of the System increases, even though a lot more people would benefit
greatly from newer changes.

## Abstract vs Concrete Code
A misguided thought that seems to be quite popular is that Code should always be reusable, 
and therefore split into many little, independent, reusable pieces.

There are multiple problems with this thought process.
First of all, finding good Abstractions can be hard or impossible for certain problems.
One reason for this is that 'beeing reusable' is actually a property of invididual Problems, and 
not something that can be 'achieved' by any programming style.
It is also harder to write abstract code, because you don't know all the possible Usages of the Code in advance.

* Focusing on concrete problems gives more context and more constrains --> more knowledge
* Code-Duplication can be avoided using Compression-Oriented programming instead of
* Specific code is simpler and doesn't need to deal with arbitrariy limitations given by the Abstraction

## Algorithms vs Infrastructure/Glue Code

## Thinking in Abstraction-Hierarchies, implementing at the low level
To solve a Coding-Problem a good first Step is always to find out more information about the extact properties
of the problem, which means figuring out 'why' and 'what parts' make the problem hard.

To figure out why a problem is hard, the typical process is as follows:
First we think about solution from similar problems, and then we look at how the problems differ and
why the easier problems don't work.
process is: Why doesn't a simple solution work? Then I think about the simplest possible solution that
I can come up with which coudl solve the problem. The first few solutions usually dont work because of some
properties of the problem. Then I come up with 

I think we usually come up with ideas and solutions for coding solutions by having abstractions, and
only through implementing something with Code we see that we didn't think all details
throught. Since code only represents the lowest possible level of abstraction, without documentation
we usually lose the thougts about the higher levels. When changing system we then have to rethink/update
the high level structure of the code by rereading the Code, and we have to keep most of the high level detail 
in our head because this is usually not written down. Usually we encode our high level thinking in
the name of functions, datatypes, comments and variables. 


## What it means to 'learn' to program, why problem solving becomes easier.
What seperates a beginner programmer from an experienced one?
Maybe by understanding the process of Learning we can figure out which criteras
make a good programmer, and how already seasoned programmers can further improve their skills.

Going from having zero knowledge about computers, lets try to reconstruct the different stages
that beginner undergo.

 1. Usually learning starts by beeing introduced to a basic subset of operations a computer can perform.
    This usually involves either simple arithmetic operations (add, subtract, divide, multiply) in
    combination with simple input output (Write to console). 
    Other simple operations (Like string concatentation in python or simple graphics like scratch) may also
    be introduced if they are easily accessible in the given language.
 1. Then they need to learn how to make the computer perform these operations, 
    e.g. how to express the given operations in the syntax of the language.
    Syntax seems to be a big hurdle for beginners, since the Syntax of the used programming language
    needs to support all possible expressable operations the language provides, not just the 
    'simple' operations the users first learn about. The stage of learning the mapping
    between the syntax and the operations the pc performs seems to be quite troublesome, and 
    a lot of common errors (e.g. parse-errors, forgetting semicolons, conversions between integers and string for output) 
    will probably be frustrating to lots of users, which may suggest that 'simpler' languages like python may
    be better for introductory programming courses.
 1. After this users are usually given smaller exercises which need to be solved with the 
    operations they were previously introduced to.
 1. Usually new concepts and operations are then introduced to the learner, and steps 1-3 are repeated.
    The problems that the leaners are required to solve usually get more complicated, and they usually
    require the newly introduced features to be solved. This way, the arsenal of programming techniques
    of the beginner grows over time, and there leaning is done in byte-sized chunks.

This poses the question about which techniques/operations are introduced to the programmer and
in which order they are introduced. I'll try to list a common sequence of techniques here:

 1. Arithmetic operations on integers and basic output
 1. Variables
 1. Input to variables
 1. Booleans, introduction to different datatypes (bool, int)
 1. Conditional Flow (If-Else)
 1. Loops 
 1. Functions, returns
 1. Arrays
 1. Basic-Operations on arrays (sum, min, max, avg)
 1. Structs/Enums
 1. Simple Algorithms (sorting)
 1. Simple Datastructures (Lists)
 1. Recursion
 1. Advanced Algorithms and Datastructures (Hashtable, Graphs, ...)

Further advanced topics and language/paradigm based ideas which are usually also taught:
 * Classes and Objects
 * OOP
 * Lambdas
 * Interfaces 
 * Programming constructs 
 * Design Patterns
 * Code Organisation
 * Exception Handling
 * Multithreading/Parallelism, Synchronization problems
 * Subroutines
 * Memory management
 * Hardware implementation/Operating system details (Threads/Processes, Interrupts, ...)

After someone has learned how a Computer might solve a given problem, 
the question becomes how is the solution implemented. 
TODO: Overengineering, Featuritis, Law of least power, Defensive/Aggressive programming

## Code Documentation/Comments, ...

Its a good question if, why and how Code-Dokumentation should be done. 
To get a introduction to this topic, lets look at some opinions on both extremes of the spectrum.

### The myth of self documenting code
When looking through coding-forums often the myth comes up that "good" Code should be self-documenting, and
that comments and other forms of documentation shouldn't be necessary if the Code is written well enough.
One should be able to undestande the purpose and meaning by simply reading the code.

Although a noble thought, in reality its actually really hard if not impossible to understand code
of complicated systems without further documentation. Reasons for this are numerous, and I'll list some of them:
 * Looking at plain source-code doesn't provide any information about its context. 
   The Context beeing: What problem does this code solve, why the problem exits + Problem specific requirements.
   Similarly to a mathematial formula without a definition of the variables in use, a plain piece of code
   is not enough to figure out what it does. Documentation can also describe the different Consideration and Discoveries
   made when coming up with the solution, alternative solutions and possible improvements
 * Code can be arbitrarily long, and its usually not possible to understand large programs all at once, but rather
   by looking at small parts and figuring out how they work, and then combining the understanding of the small
   parts to understand the whole program. Even this may be a simplification of the process of understanding a system,
   since sometimes understanding a sub-part requires understanding the global problem (lack of context), and vice versa.
   This is sometimes refered to as the hermeneutical spiral. The process of understanding such a system then becomes
   very cumbersome, since the whole and sub parts need to be re-read and re-contextualized multiple times before
   the system is understood.
 * For Code-Parts that need to be performant or are generally complicated, a more thorough explanation is usually
   necessary for other people to understand the code. For example, I am quite sure that nobody is able to figure out 
   what dijstras shortest-path algorithm does by simply looking at the code alone.

Does this mean that we should document our code as much as possible? Not exactly, but here I want
to mention an idea called Literate-Programming, which takes this idea to the extremes.
In literate-programming instead of just writing code you write the Documentation/Explanation of what the code does
similar to a book (including rich text, images, formatting...), which itself contains small code-snippets at appropriate places, 
and the whole document (for example Latex) can then be "compiled", which gathers all the small Snippets
inside the document and puts them together as the real source code (Without the documentation), which can then be 
passed to a real compiler.

One of the thoughts behind this is that we don't share enough Knowledge/ code/ TODO

The problem with excessive documentation is the following:
 * Documentation requires Time and Effort, and the benefits need to outweigh its costs
 * Documentation needs to be updated, since it is basically a part of your Source-Code. 
   This also means that Documentation increases the size of your program, which slows
   down the rate at which you can change Code.
 * Documentation may become invalid, since it cannot be checked by a compiler or similar tools, unlike 
   normal Code, where static checks and tests can be performed.

## Why is Coding Text-Based? Couldn't other (e.g. graphical) representations/editors be more helpful?

The question would be how these other representations deal with the following Aspects:
 * How are programming strutures expressed (statements, expression, control-flow, Symbols/Naming)
 * If 2D/3D, what should the Position/Orientation/Size of Things represent?
 * Editing/Modifying Code.
 * Is correct syntax enforced? How to edit Code and follow the syntax rules
 * What benefits do other representations give? How do they outweight the disadvantages?

My solutions in Upp:
 * Token-Based editing, which still allowes all text operations to be performed
 * Automated formating restricts the set of expressable programs with the same effect
 * Visualizations and Text-Markup should be easily addable by the compiler 
 * Block-Based approach

## Other thoughts/quotes that need more thinking about
 * Mouse is slower then Keyboard is slower then Thoughts
 * Code is read more often then it is written
