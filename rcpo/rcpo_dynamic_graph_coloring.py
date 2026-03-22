import networkx as nx
import math
import random
from queue import PriorityQueue
import matplotlib.pyplot as plt


class DincIndex:
    def __init__(self):
        self.cnt = {}
        self.cu = set({})


class DcSimpleAlgo:
    def __init__(self, G: nx.Graph = nx.Graph(), p: int = 0.5, max_colors: int = 6):
        self.G = G.copy()                                         # Undirected graph G
        self.Gstar = nx.DiGraph()                                 # Directed graph Gstar, used for everything within the algorithm
        self.changeCounter = 0                                    # Initialize changeCounter to 0
        self.p = p                                                # Probability of taking a random step
        self.MAX_COLORS = max_colors                              # Maximum number of colors allowed
        self.pending_changes = []                                 # Track pending batch changes

        self.elemCounter = 0                        # Counter for elementary operations

        self.Gstar.add_nodes_from(self.G.nodes())
        nx.set_node_attributes(self.Gstar, 0, 'color')                      # Reset all colors to 0
        nx.set_node_attributes(self.Gstar, 0, 'changed')               # Reset all change counters to 0
        for node in self.Gstar.nodes():
            self.Gstar.nodes[node]['DINC'] = DincIndex()               # Initialize all DINC indices

        # Use greedy coloring for initial state
        if len(self.G.nodes()) > 0:
            self.greedyInitialColoring()
            
            # Then build the orientation
            for edge in self.G.edges():
                self.dcOrientInsert(edge[0], edge[1], override=True)


    def greedyInitialColoring(self):
        """Apply greedy coloring sorted by degree (highest first) to minimize initial colors"""
        nodes_by_degree = sorted(self.G.nodes(), 
                                key=lambda n: self.G.degree(n), 
                                reverse=True)
        
        for node in nodes_by_degree:
            # Find colors used by neighbors
            neighbor_colors = set()
            for neighbor in self.G.neighbors(node):
                neighbor_colors.add(self.Gstar.nodes[neighbor]['color'])
            
            # Assign lowest available color within cap
            color_assigned = False
            for color in range(self.MAX_COLORS):
                if color not in neighbor_colors:
                    self.Gstar.nodes[node]['color'] = color
                    color_assigned = True
                    break
            
            if not color_assigned:
                # Cannot color this node validly - assign color 0 and warn
                self.Gstar.nodes[node]['color'] = 0
                print(f"Warning: Node {node} cannot be validly colored during initialization")


    def nodePriority(self, node):
        return (-1*self.Gstar.degree(node), list(self.Gstar.nodes()).index(node))


    # Returns wheter a comes before b in the ordering of nodes
    def isBefore(self, a, b):
        if self.nodePriority(a) < self.nodePriority(b):
            return True
        else:
            return False


    def collectColor(self, u):
        """Collect the lowest available color for node u, respecting MAX_COLORS cap"""
        I = self.Gstar.nodes[u]['DINC']
        
        # Check all colors from 0 to MAX_COLORS-1 for the lowest available
        for i in range(self.MAX_COLORS):
            if I.cnt.get(i, 0) == 0:
                return i
        
        # No valid color available within the cap
        return None


    def assignColor(self, u, Cnew):
        for edge in self.Gstar.out_edges(u):
            v = edge[1]
            self.dincColorDecrease(v, u)
            self.dincColorIncrease(v, c=Cnew)
        self.Gstar.nodes[u]['color'] = Cnew
        self.Gstar.nodes[u]['DINC'].cu.clear()
        self.changeCounter += 1      # update change counter
        self.Gstar.nodes[u]['changed'] = self.changeCounter


    def notifyColor(self, u, Cold: int, q: PriorityQueue):
        for edge in self.Gstar.out_edges(u):
            v = edge[1]
            if (not (self.nodePriority(v), v) in q.queue) and (self.Gstar.nodes[u]['color'] == self.Gstar.nodes[v]['color'] or Cold < self.Gstar.nodes[v]['color']):
                q.put((self.nodePriority(v), v))


    def handleColoringConflict(self, u, q: PriorityQueue):
        """
        When node u cannot find a valid color, force recoloring of a neighbor
        to free up a color slot. Choose the neighbor that was most recently changed
        to minimize total changes.
        """
        # Find which colors are occupied by neighbors
        occupied = set()
        neighbor_list = []
        
        for edge in self.Gstar.in_edges(u):
            v = edge[0]
            color_v = self.Gstar.nodes[v]['color']
            occupied.add(color_v)
            neighbor_list.append((self.Gstar.nodes[v]['changed'], v, color_v))
        
        # Sort by most recently changed (highest changeCounter)
        neighbor_list.sort(reverse=True)
        
        # Force recolor the most recently changed neighbor to free a slot
        for _, v, old_color in neighbor_list:
            # Try to find an alternate color for v
            v_neighbors_colors = set()
            for n in self.G.neighbors(v):
                if n != u:
                    v_neighbors_colors.add(self.Gstar.nodes[n]['color'])
            
            for new_color in range(self.MAX_COLORS):
                if new_color not in v_neighbors_colors:
                    # Found a valid recoloring for v
                    self.assignColor(v, new_color)
                    q.put((self.nodePriority(v), v))
                    q.put((self.nodePriority(u), u))
                    return
        
        # If we get here, the graph cannot be colored with MAX_COLORS colors
        # Assign color 0 (or keep current) - this may create an invalid coloring
        if self.Gstar.nodes[u]['color'] >= self.MAX_COLORS:
            self.assignColor(u, 0)
        print(f"Warning: Node {u} cannot be validly colored with {self.MAX_COLORS} colors")


    def CAN(self, q: PriorityQueue):
        while not q.empty():
            u = q.get()[1]
            Cnew = self.collectColor(u)
            if Cnew != None:
                Cold = self.Gstar.nodes[u]['color']
                self.assignColor(u, Cnew)
                self.notifyColor(u, Cold, q)
            else:
                # No valid color found - need to trigger cascading recoloring
                self.handleColoringConflict(u, q)


    def dincColorIncrease(self, u, v=None, c=None):
        I = self.Gstar.nodes[u]['DINC']
        if v != None:
            c = self.Gstar.nodes[v]['color']
        elif c == None:
            return
        
        if I.cnt.get(c, 0) != 0:
            I.cnt[c] += 1
        else:
            I.cnt[c] = 1
        if c in I.cu:
            I.cu.remove(c)


    def dincColorDecrease(self, u, v):
        I = self.Gstar.nodes[u]['DINC']
        c = self.Gstar.nodes[v]['color']
       
        if I.cnt.get(c, 0) > 0:
            I.cnt[c] = I.cnt[c]-1
        if I.cnt.get(c, 0) == 0:
            if c in I.cnt:
                I.cnt.pop(c)
       
        if I.cnt.get(c, 0) == 0 and c < self.Gstar.nodes[u]['color']:
            I.cu.add(c)


    def ocgInsert(self, u, v):
        S = set({})
        S.add(u)
        S.add(v)

        uEdges = list(self.Gstar.in_edges(u)).copy()
        vEdges = list(self.Gstar.in_edges(v)).copy()

        self.Gstar.add_edge(u, v)

        for edge in uEdges:
            nbr = edge[0]
            if self.isBefore(u, nbr):
                self.Gstar.remove_edge(nbr, u)
                self.Gstar.add_edge(u, nbr)
                self.dincColorIncrease(nbr, u)
                self.dincColorDecrease(u, nbr)
                S.add(nbr)
        
        for edge in vEdges:
            nbr = edge[0]
            if self.isBefore(v, nbr):
                self.Gstar.remove_edge(nbr, v)
                self.Gstar.add_edge(v, nbr)
                self.dincColorIncrease(nbr, v)
                self.dincColorDecrease(v, nbr)
                S.add(nbr)
        self.dincColorIncrease(v, u)
        return S


    def ocgDelete(self, u, v):
        S = set({})
        S.add(u)
        S.add(v)

        if not self.Gstar.has_edge(u, v):
            # If this edge is not in Gstar it must be present in the opposite direction
            x = u
            u = v
            v = x
        self.Gstar.remove_edge(u, v)

        for edge in list(self.Gstar.out_edges(u)).copy():
            nbr = edge[1]
            if self.isBefore(nbr, u):
                self.Gstar.remove_edge(u, nbr)
                self.Gstar.add_edge(nbr, u)
                self.dincColorIncrease(u, nbr)
                self.dincColorDecrease(nbr, u)
                S.add(nbr)
        
        for edge in list(self.Gstar.out_edges(v)).copy():
            nbr = edge[1]
            if self.isBefore(nbr, v):
                self.Gstar.remove_edge(v, nbr)
                self.Gstar.add_edge(nbr, v)
                self.dincColorIncrease(v, nbr)
                self.dincColorDecrease(nbr, v)
                S.add(nbr)
        self.dincColorDecrease(v, u)
        return S


    def dcOrientInsert(self, u, v, override=False):
        b = random.uniform(0, 1) <= self.p
        if b and not override:
            if self.isBefore(u, v):
                S = self.ocgInsert(u, v)
            else:
                S = self.ocgInsert(v, u)
            # Check if colors of endpoints are the same, if so, choose a new color for one of the neighbours
            # Recolor the neighbor that has most recently been recolored. If this value is the same (only possible on 'new' nodes) pick one at random
            if self.Gstar.nodes[u]['color'] == self.Gstar.nodes[v]['color']:
                if self.Gstar.nodes[u]['changed'] > self.Gstar.nodes[v]['changed']:
                    self.recolor(u)
                elif self.Gstar.nodes[v]['changed'] > self.Gstar.nodes[u]['changed']:
                    self.recolor(v)
                elif bool(random.getrandbits(1)):
                    self.recolor(u)
                else:
                    self.recolor(v)
        else:
            q = PriorityQueue()
            if self.isBefore(u, v):
                S = self.ocgInsert(u, v)
            else:
                S = self.ocgInsert(v, u)
            for w in S:
                q.put((self.nodePriority(w), w))
            self.CAN(q)


    def dcOrientDelete(self, u, v):
        # Chance for a randomized step can be added here, much like with insert
        b = random.uniform(0, 1) <= self.p
        if b:
            self.ocgDelete(u, v)
        else:
            q = PriorityQueue()
            S = self.ocgDelete(u, v)
            for w in S:
                q.put((self.nodePriority(w), w))
            self.CAN(q)

    def recolor(self, node):
        """Recolor a node with the lowest available color within MAX_COLORS"""
        self.changeCounter += 1
        self.Gstar.nodes[node]['changed'] = self.changeCounter

        # Find occupied colors by neighbors
        neighbors = list(self.G.neighbors(node))
        occupiedColors = set()
        for neighbor in neighbors:
            occupiedColors.add(self.Gstar.nodes[neighbor]['color'])

        # Find lowest available color within cap
        Cnew = None
        for i in range(self.MAX_COLORS):
            if i not in occupiedColors:
                Cnew = i
                break

        if Cnew is None:
            # No valid color found - assign color 0 (may create conflict)
            Cnew = 0
            print(f"Warning: Cannot recolor node {node} validly with {self.MAX_COLORS} colors")

        # Update DINC-Index
        for edge in self.Gstar.out_edges(node):
            nbr = edge[1]
            self.dincColorDecrease(nbr, node)
            self.dincColorIncrease(nbr, c=Cnew)
        self.Gstar.nodes[node]['color'] = Cnew

    # Returns a coloring dictionary from the nodes 'color' attributes
    def getColoring(self) -> dict:
        coloring: dict = {}
        for node in self.Gstar.nodes():
            coloring[node] = self.Gstar.nodes[node]['color']
        return coloring
    
    
    def isValidColoring(self) -> bool:
        """Check if the current coloring is valid (no adjacent vertices have same color)"""
        for edge in self.G.edges():
            u, v = edge
            if self.Gstar.nodes[u]['color'] == self.Gstar.nodes[v]['color']:
                return False
        return True

    def showColoredGraph(self):
        """Visualize the current coloring of G using matplotlib."""
        coloring = self.getColoring()   # <-- FIXED: correctly fetch coloring
        G = self.G
        
        if len(G.nodes()) == 0:
            print("Graph is empty — nothing to draw.")
            return
    
        # Color list for all nodes
        node_colors = [coloring[n] for n in G.nodes()]
    
        plt.figure(figsize=(4, 4))
        pos = nx.spring_layout(G, seed=42)
        nx.draw(
            G, pos,
            with_labels=True,
            node_color=node_colors,
            cmap=plt.cm.tab10,
            edgecolors="black",
            linewidths=1.0
        )
        plt.title("Current Graph Coloring")
        plt.tight_layout()
        plt.show()
    
    def getColoringStats(self) -> dict:
        """Return statistics about the current coloring"""
        coloring = self.getColoring()
        colors_used = set(coloring.values())

        self.showColoredGraph()
        
        return {
            'num_colors': len(colors_used),
            'colors_used': sorted(colors_used),
            'is_valid': self.isValidColoring(),
            'total_changes': self.changeCounter
        }


    # ============== BATCH UPDATE METHODS ==============

    def addEdgeLazy(self, s, t):
        """Add edge without triggering recoloring - queues for batch update"""
        if self.G.has_edge(s, t):
            return
        if not self.G.has_node(s) or not self.G.has_node(t):
            return
        
        self.G.add_edge(s, t)
        self.pending_changes.append(('add_edge', s, t))


    def removeEdgeLazy(self, s, t):
        """Remove edge without triggering recoloring - queues for batch update"""
        if not self.G.has_edge(s, t):
            return
        
        self.G.remove_edge(s, t)
        self.pending_changes.append(('remove_edge', s, t))


    def addVertexLazy(self, v):
        """Add vertex without triggering recoloring - queues for batch update"""
        if self.G.has_node(v):
            return
        
        self.G.add_node(v)
        self.pending_changes.append(('add_vertex', v, None))


    def removeVertexLazy(self, v):
        """Remove vertex without triggering recoloring - queues for batch update"""
        if not self.G.has_node(v):
            return
        
        # Remove all edges incident to this vertex first
        edges_to_remove = list(self.G.edges(v))
        for edge in edges_to_remove:
            self.G.remove_edge(edge[0], edge[1])
        
        self.G.remove_node(v)
        self.pending_changes.append(('remove_vertex', v, None))


    def updateColoring(self):
        """
        Apply all pending changes and recompute coloring.
        This is more efficient than individual updates when making multiple changes.
        """
        if len(self.pending_changes) == 0:
            return
        
        print(f"Applying {len(self.pending_changes)} pending changes...")
        
        # Clear Gstar and rebuild from current G state
        self.Gstar.clear()
        self.Gstar.add_nodes_from(self.G.nodes())
        nx.set_node_attributes(self.Gstar, 0, 'color')
        nx.set_node_attributes(self.Gstar, 0, 'changed')
        
        for node in self.Gstar.nodes():
            self.Gstar.nodes[node]['DINC'] = DincIndex()
        
        # Recolor from scratch with greedy algorithm
        if len(self.G.nodes()) > 0:
            self.greedyInitialColoring()
            
            # Rebuild orientation structure
            for edge in self.G.edges():
                self.dcOrientInsert(edge[0], edge[1], override=True)
        
        # Clear pending changes
        self.pending_changes.clear()
        print(f"Coloring updated. Total changes so far: {self.changeCounter}")


    def clearPendingChanges(self):
        """Clear all pending changes without applying them (revert to last updateColoring state)"""
        # Rebuild G from Gstar (revert to last stable state)
        self.G.clear()
        self.G.add_nodes_from(self.Gstar.nodes())
        for u in self.Gstar.nodes():
            for v in self.Gstar.successors(u):
                if not self.G.has_edge(u, v):
                    self.G.add_edge(u, v)
        
        self.pending_changes.clear()